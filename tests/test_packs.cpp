#include "mcpp/image_png.hpp"
#include "mcpp/packs/mipmap.hpp"
#include "mcpp/packs/pack_apply.hpp"
#include "mcpp/packs/pack_cache.hpp"
#include "mcpp/packs/pack_protocol.hpp"
#include "mcpp/packs/pack_system.hpp"
#include "mcpp/packs/resource_pack.hpp"
#include "mcpp/packs/zip.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;
using namespace mcpp::packs;

namespace {

int failures = 0;
int skipped = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// A zip written by hand, stored or deflated, so the test builds its own pack
// instead of needing one on disk.
class ZipWriter {
public:
    void add(const std::string& name, const std::vector<uint8_t>& bytes, bool deflate = false) {
        Entry entry;
        entry.name = name;
        entry.crc = static_cast<uint32_t>(crc32(0, bytes.data(),
                                                static_cast<unsigned>(bytes.size())));
        entry.uncompressed = bytes.size();
        entry.offset = body_.size();
        std::vector<uint8_t> payload = bytes;
        entry.method = 0;
        if (deflate) {
            std::vector<uint8_t> out(bytes.size() + 128);
            z_stream stream{};
            deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            stream.next_in = const_cast<uint8_t*>(bytes.data());
            stream.avail_in = static_cast<unsigned>(bytes.size());
            stream.next_out = out.data();
            stream.avail_out = static_cast<unsigned>(out.size());
            ::deflate(&stream, Z_FINISH);
            out.resize(stream.total_out);
            deflateEnd(&stream);
            payload = std::move(out);
            entry.method = 8;
        }
        entry.compressed = payload.size();
        put32(body_, 0x04034b50);
        put16(body_, 20);
        put16(body_, 0);
        put16(body_, entry.method);
        put16(body_, 0);
        put16(body_, 0);
        put32(body_, entry.crc);
        put32(body_, static_cast<uint32_t>(entry.compressed));
        put32(body_, static_cast<uint32_t>(entry.uncompressed));
        put16(body_, static_cast<uint16_t>(name.size()));
        put16(body_, 0);
        body_.insert(body_.end(), name.begin(), name.end());
        body_.insert(body_.end(), payload.begin(), payload.end());
        entries_.push_back(std::move(entry));
    }

    void add_text(const std::string& name, std::string_view text, bool deflate = false) {
        add(name, std::vector<uint8_t>(text.begin(), text.end()), deflate);
    }

    std::vector<uint8_t> finish() const {
        std::vector<uint8_t> out = body_;
        const size_t directory_at = out.size();
        for (const Entry& entry : entries_) {
            put32(out, 0x02014b50);
            put16(out, 20);
            put16(out, 20);
            put16(out, 0);
            put16(out, entry.method);
            put16(out, 0);
            put16(out, 0);
            put32(out, entry.crc);
            put32(out, static_cast<uint32_t>(entry.compressed));
            put32(out, static_cast<uint32_t>(entry.uncompressed));
            put16(out, static_cast<uint16_t>(entry.name.size()));
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, 0);
            put32(out, static_cast<uint32_t>(entry.offset));
            out.insert(out.end(), entry.name.begin(), entry.name.end());
        }
        const size_t directory_size = out.size() - directory_at;
        put32(out, 0x06054b50);
        put16(out, 0);
        put16(out, 0);
        put16(out, static_cast<uint16_t>(entries_.size()));
        put16(out, static_cast<uint16_t>(entries_.size()));
        put32(out, static_cast<uint32_t>(directory_size));
        put32(out, static_cast<uint32_t>(directory_at));
        put16(out, 0);
        return out;
    }

private:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t crc = 0;
        size_t compressed = 0;
        size_t uncompressed = 0;
        size_t offset = 0;
    };
    static void put16(std::vector<uint8_t>& out, uint16_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>(value >> 8));
    }
    static void put32(std::vector<uint8_t>& out, uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
        }
    }
    std::vector<uint8_t> body_;
    std::vector<Entry> entries_;
};

// A flat square of one colour, as a png, which is what a pack's textures are.
std::vector<uint8_t> flat_png(uint32_t side, uint32_t rgba) {
    const fs::path path = fs::temp_directory_path() /
                          std::format("plasma-pack-test-{}-{:08x}.png", side, rgba);
    std::vector<uint8_t> pixels(static_cast<size_t>(side) * side * 4);
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i] = static_cast<uint8_t>(rgba >> 24);
        pixels[i + 1] = static_cast<uint8_t>(rgba >> 16);
        pixels[i + 2] = static_cast<uint8_t>(rgba >> 8);
        pixels[i + 3] = static_cast<uint8_t>(rgba);
    }
    mcpp::write_png(path, side, side, pixels);
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    std::error_code code;
    fs::remove(path, code);
    return bytes;
}

constexpr std::string_view kMeta = R"({"pack":{"description":"a test pack",)"
                                   R"("min_format":[75,0],"max_format":[75,2147483647]}})";

PackId id_of(uint8_t seed) {
    PackId id{};
    for (size_t i = 0; i < id.size(); ++i) id[i] = static_cast<uint8_t>(seed + i);
    return id;
}

// A target that keeps what was written, so the whole apply path runs without
// a gpu and the test can look at what landed where.
class RecordingTarget : public PackTarget {
public:
    RecordingTarget(uint32_t tile, uint32_t mips, uint32_t layers)
        : tile_(tile), mips_(mips), layers_(layers) {}

    uint32_t atlas_tile() const override { return tile_; }
    uint32_t atlas_mips() const override { return mips_; }
    uint32_t atlas_layers() const override { return layers_; }
    double patch_atlas(std::span<const LayerPatch> patches) override {
        for (const LayerPatch& patch : patches) {
            atlas[patch.layer] = patch.levels;
            ++atlas_writes;
        }
        return 0.25;
    }

    uint32_t item_tile() const override { return 32; }
    int32_t item_layer_of(std::string_view name) const override {
        const auto found = items.find(std::string(name));
        return found == items.end() ? -1 : found->second;
    }
    bool set_item_icon(int32_t layer, const Bitmap& icon) override {
        if (icon.width != 32 || icon.height != 32) return false;
        icons[layer] = icon;
        return true;
    }

    std::array<uint32_t, 4> sprite_rect(std::string_view name) const override {
        const auto found = rects.find(std::string(name));
        return found == rects.end() ? std::array<uint32_t, 4>{0, 0, 0, 0} : found->second;
    }
    bool set_sprite_pixels(std::string_view name, const Bitmap& pixels) override {
        sprites[std::string(name)] = pixels;
        return true;
    }

    std::map<uint32_t, std::vector<std::vector<uint8_t>>> atlas;
    std::map<int32_t, Bitmap> icons;
    std::map<std::string, Bitmap> sprites;
    std::map<std::string, int32_t> items;
    std::map<std::string, std::array<uint32_t, 4>> rects;
    size_t atlas_writes = 0;

private:
    uint32_t tile_, mips_, layers_;
};

void test_protocol_values() {
    check(static_cast<int32_t>(Action::SuccessfullyLoaded) == 0 &&
              static_cast<int32_t>(Action::Declined) == 1 &&
              static_cast<int32_t>(Action::FailedDownload) == 2 &&
              static_cast<int32_t>(Action::Accepted) == 3 &&
              static_cast<int32_t>(Action::Downloaded) == 4 &&
              static_cast<int32_t>(Action::InvalidUrl) == 5 &&
              static_cast<int32_t>(Action::FailedReload) == 6 &&
              static_cast<int32_t>(Action::Discarded) == 7,
          "the eight actions are in the order the enum goes on the wire");
    check(!is_terminal(Action::Accepted) && !is_terminal(Action::Downloaded) &&
              is_terminal(Action::SuccessfullyLoaded) && is_terminal(Action::Declined),
          "accepted and downloaded are progress, the rest are answers");
    check(action_of(Update::Accepted) == Action::Accepted &&
              action_of(Update::Downloaded) == Action::Downloaded &&
              action_of(FinalResult::Applied) == Action::SuccessfullyLoaded &&
              action_of(FinalResult::DownloadFailed) == Action::FailedDownload &&
              action_of(FinalResult::Declined) == Action::Declined &&
              action_of(FinalResult::Discarded) == Action::Discarded &&
              action_of(FinalResult::ActivationFailed) == Action::FailedReload,
          "every feedback maps to the action vanilla sends for it");

    check(pack_url_allowed("http://127.0.0.1:8080/p.zip"), "http is fetched");
    check(pack_url_allowed("HTTPS://host/p.zip"), "the scheme is not case sensitive");
    check(!pack_url_allowed("file:///etc/passwd"), "file is not");
    check(!pack_url_allowed("ftp://host/p.zip"), "nor is ftp");
    check(!pack_url_allowed("not a url"), "nor is nonsense");

    check(parse_sha1(std::string(40, 'A')) == std::string(40, 'a'), "a hash is lowercased");
    check(!parse_sha1(std::string(39, 'a')).has_value(), "a short hash is no hash");
    check(!parse_sha1("g" + std::string(39, 'a')).has_value(), "a non-hex hash is no hash");

    check(prompt_status_from_option("prompt") == PromptStatus::Pending &&
              prompt_status_from_option("enabled") == PromptStatus::Allowed &&
              prompt_status_from_option("disabled") == PromptStatus::Declined,
          "the three serverResourcePacks settings read back");
}

// The order of the answers for one accepted pack, and for one that is popped.
void test_state_machine() {
    std::vector<std::pair<PackId, Action>> sent;
    std::vector<ServerPackManager::IdAndPath> reload;
    std::vector<ServerPackManager::DownloadRequest> asked;

    // The reload reports back the way `DownloadedPackSource.startReload` does,
    // so the test runs the whole sequence rather than half of it.
    ServerPackManager* owner = nullptr;
    ServerPackManager::Hooks hooks;
    hooks.respond = [&](const PackId& id, Action action) { sent.emplace_back(id, action); };
    hooks.download = [&](std::vector<ServerPackManager::DownloadRequest> batch) { asked = batch; };
    hooks.schedule_reload = [&](std::vector<ServerPackManager::IdAndPath> packs) {
        reload = std::move(packs);
        owner->reload_succeeded();
    };
    ServerPackManager manager(std::move(hooks), PromptStatus::Allowed);
    owner = &manager;

    const PackId one = id_of(1);
    manager.push_pack(one, "http://host/a.zip", std::string(40, 'a'));
    manager.run_updates();
    check(sent.size() == 1 && sent[0].second == Action::Accepted,
          "an allowed pack is accepted before anything is fetched");
    check(asked.size() == 1 && asked[0].id == one, "and then asked for");

    ServerPackManager::BatchResult result;
    result.downloaded.emplace_back(one, "/tmp/a.zip");
    manager.deliver_download(result);
    manager.run_updates();
    check(sent.size() == 3 && sent[1].second == Action::Downloaded, "downloaded comes next");
    check(reload.size() == 1 && reload[0].id == one, "and the reload is asked for the pack");
    check(sent[2].second == Action::SuccessfullyLoaded,
          "a reload that succeeded answers successfully_loaded");

    sent.clear();
    reload.clear();
    manager.pop_pack(one);
    manager.run_updates();
    check(reload.empty(), "a popped pack leaves nothing to load");
    check(sent.empty(), "and the server is told nothing: it already knows");
    check(manager.tracked() == 0, "the pack is forgotten");
}

void test_declining() {
    std::vector<Action> sent;
    ServerPackManager::Hooks hooks;
    hooks.respond = [&](const PackId&, Action action) { sent.push_back(action); };
    ServerPackManager manager(std::move(hooks), PromptStatus::Declined);
    manager.push_pack(id_of(2), "http://host/a.zip", std::nullopt);
    manager.run_updates();
    check(sent.size() == 1 && sent[0] == Action::Declined,
          "a pack pushed to a player who said never is declined at once");

    sent.clear();
    ServerPackManager::Hooks waiting;
    waiting.respond = [&](const PackId&, Action action) { sent.push_back(action); };
    ServerPackManager pending(std::move(waiting), PromptStatus::Pending);
    pending.push_pack(id_of(3), "http://host/a.zip", std::nullopt);
    pending.run_updates();
    check(sent.empty(), "a pending prompt sends nothing until the player answers");
    pending.reject_server_packs();
    pending.run_updates();
    check(sent.size() == 1 && sent[0] == Action::Declined, "and no is declined");
}

void test_download_failure() {
    std::vector<Action> sent;
    ServerPackManager::Hooks hooks;
    hooks.respond = [&](const PackId&, Action action) { sent.push_back(action); };
    hooks.download = [](std::vector<ServerPackManager::DownloadRequest>) {};
    ServerPackManager manager(std::move(hooks), PromptStatus::Allowed);
    const PackId id = id_of(4);
    manager.push_pack(id, "http://host/a.zip", std::string(40, 'b'));
    manager.run_updates();
    ServerPackManager::BatchResult result;
    result.failed.push_back(id);
    manager.deliver_download(result);
    manager.run_updates();
    check(sent.size() == 2 && sent[1] == Action::FailedDownload,
          "a pack that did not arrive answers failed_download");
}

void test_two_packs_stack() {
    std::vector<ServerPackManager::IdAndPath> reload;
    ServerPackManager* owner = nullptr;
    ServerPackManager::Hooks hooks;
    hooks.respond = [](const PackId&, Action) {};
    hooks.download = [](std::vector<ServerPackManager::DownloadRequest>) {};
    hooks.schedule_reload = [&](std::vector<ServerPackManager::IdAndPath> packs) {
        reload = std::move(packs);
        owner->reload_succeeded();
    };
    ServerPackManager manager(std::move(hooks), PromptStatus::Allowed);
    owner = &manager;
    const PackId first = id_of(5);
    const PackId second = id_of(9);
    manager.push_pack(first, "http://host/a.zip", std::nullopt);
    manager.push_pack(second, "http://host/b.zip", std::nullopt);
    manager.run_updates();
    ServerPackManager::BatchResult result;
    result.downloaded.emplace_back(first, "/tmp/a.zip");
    result.downloaded.emplace_back(second, "/tmp/b.zip");
    manager.deliver_download(result);
    manager.run_updates();
    check(reload.size() == 2 && reload[0].id == first && reload[1].id == second,
          "two packs load in the order the server pushed them");

    // The same id pushed again replaces the one that was there.
    manager.push_pack(first, "http://host/c.zip", std::nullopt);
    manager.run_updates();
    check(manager.applied().size() <= 2, "a replaced pack does not pile up");
}

void test_zip() {
    ZipWriter writer;
    writer.add_text("pack.mcmeta", kMeta);
    writer.add_text("assets/minecraft/stored.txt", "stored", false);
    writer.add_text("assets/minecraft/deflated.txt",
                    std::string(4096, 'x') + "end", true);

    Zip zip;
    check(zip.open(writer.finish()), "a zip we wrote opens");
    check(zip.names().size() == 3, "all three entries are indexed");
    const auto stored = zip.read("assets/minecraft/stored.txt");
    check(stored.has_value() && std::string(stored->begin(), stored->end()) == "stored",
          "a stored entry reads back");
    const auto deflated = zip.read("assets/minecraft/deflated.txt");
    check(deflated.has_value() && deflated->size() == 4099 && deflated->back() == 'd',
          "a deflated entry inflates to what went in");
    check(!zip.read("nothing").has_value(), "an entry that is not there reads nothing");

    // The caps.
    ZipLimits small;
    small.archive_bytes = 32;
    Zip tiny;
    check(!tiny.open(writer.finish(), small), "an archive over the size cap is refused");
    check(tiny.error().find("over the") != std::string::npos, "and says so");

    ZipLimits per_entry;
    per_entry.entry_bytes = 100;
    Zip narrow;
    check(!narrow.open(writer.finish(), per_entry),
          "an entry that inflates past the cap is refused before a byte is allocated");

    check(!zip_name_safe("../outside"), "a name climbing out is not safe");
    check(!zip_name_safe("/etc/passwd"), "nor is an absolute one");
    check(!zip_name_safe("a/../../b"), "nor is one that climbs in the middle");
    check(zip_name_safe("assets/minecraft/textures/block/stone.png"), "a pack path is");

    ZipWriter escaping;
    escaping.add_text("../escape.txt", "no");
    Zip refused;
    check(!refused.open(escaping.finish()), "a zip with such a name does not open at all");

    std::vector<uint8_t> rubbish(512, 0x41);
    Zip not_a_zip;
    check(!not_a_zip.open(rubbish), "bytes that are not a zip are refused");
}

void test_pack_metadata() {
    ZipWriter writer;
    writer.add_text("pack.mcmeta", kMeta);
    writer.add("assets/minecraft/textures/block/stone.png", flat_png(16, 0xFF0000FFu));
    ResourcePack pack;
    check(pack.open_bytes(writer.finish()), "a pack with a pack.mcmeta opens");
    check(pack.meta().compatible, "a pack made for this format is compatible");
    check(pack.meta().description == "a test pack", "the description reads back");
    check(pack.has("assets/minecraft/textures/block/stone.png"), "its texture is there");
    check(pack.list("assets/minecraft/textures/", ".png").size() == 1, "and is the only one");

    ZipWriter bare;
    bare.add_text("assets/minecraft/textures/block/stone.png", "not a png");
    ResourcePack no_meta;
    check(!no_meta.open_bytes(bare.finish()),
          "a pack with no pack.mcmeta does not open, the way vanilla refuses the reload");

    ZipWriter old;
    old.add_text("pack.mcmeta", R"({"pack":{"description":"old","pack_format":34}})");
    ResourcePack ancient;
    check(ancient.open_bytes(old.finish()), "a pack with a bare pack_format still opens");
    check(!ancient.meta().compatible, "but is not compatible with this version");

    // An overlay that applies, and one that does not.
    ZipWriter overlaid;
    overlaid.add_text("pack.mcmeta", R"({"pack":{"description":"o","min_format":[75,0],)"
                                     R"("max_format":[75,2147483647]},)"
                                     R"("overlays":{"entries":[)"
                                     R"({"overlay":"new","min_format":[70,0],)"
                                     R"("max_format":[80,0]},)"
                                     R"({"overlay":"old","min_format":[1,0],)"
                                     R"("max_format":[20,0]}]}})");
    overlaid.add("assets/minecraft/textures/block/stone.png", flat_png(16, 0xFF0000FFu));
    overlaid.add("new/assets/minecraft/textures/block/stone.png", flat_png(16, 0x00FF00FFu));
    overlaid.add("old/assets/minecraft/textures/block/stone.png", flat_png(16, 0x0000FFFFu));
    ResourcePack with_overlay;
    check(with_overlay.open_bytes(overlaid.finish()), "a pack with overlays opens");
    check(with_overlay.meta().overlays.size() == 1 && with_overlay.meta().overlays[0] == "new",
          "only the overlay whose range covers this version applies");
    const auto chosen = with_overlay.read("assets/minecraft/textures/block/stone.png");
    const auto image = chosen.has_value() ? mcpp::read_png(*chosen)
                                          : std::optional<mcpp::Image>{};
    check(image.has_value() && image->rgba[1] == 0xFF && image->rgba[0] == 0x00,
          "and its picture is the one that is read");
    check(!overlay_dir_valid("../x") && !overlay_dir_valid("a/b") && overlay_dir_valid("new.1"),
          "an overlay directory name is checked the way vanilla checks it");
}

void test_stack_order() {
    ZipWriter bottom;
    bottom.add_text("pack.mcmeta", kMeta);
    bottom.add("assets/minecraft/textures/block/stone.png", flat_png(16, 0xFF0000FFu));
    ZipWriter top;
    top.add_text("pack.mcmeta", kMeta);
    top.add("assets/minecraft/textures/block/stone.png", flat_png(16, 0x00FF00FFu));

    PackStack stack;
    check(stack.add_bytes(bottom.finish()) && stack.add_bytes(top.finish()),
          "two packs go on the stack");
    const auto bytes = stack.read("assets/minecraft/textures/block/stone.png");
    const auto image = bytes.has_value() ? mcpp::read_png(*bytes)
                                         : std::optional<mcpp::Image>{};
    check(image.has_value() && image->rgba[1] == 0xFF,
          "the pack pushed last is the one that is read");
}

void test_hashing_and_cache() {
    const fs::path directory = fs::temp_directory_path() / "plasma-pack-cache-test";
    std::error_code code;
    fs::remove_all(directory, code);
    PackCache cache(directory);

    const std::vector<uint8_t> bytes{'p', 'a', 'c', 'k'};
    check(sha1_hex(bytes) == "6acd621280932e6f3f2f8783e06992590b6c7a53",
          "a sha1 is the sha1 sha1sum gives");
    const auto stored = cache.store(id_of(7), bytes);
    check(stored.ok() && stored.path.filename() == sha1_hex(bytes),
          "a pack is kept under its own hash, the way vanilla's cache is keyed");

    // The cached file is used when the hash matches, and not when it does not.
    ServerPackManager::DownloadRequest matching;
    matching.id = id_of(7);
    matching.url = "http://127.0.0.1:1/never-reached.zip";
    matching.hash = sha1_hex(bytes);
    const auto hit = cache.fetch(matching);
    check(hit.ok() && hit.from_cache, "a cache hit needs no network at all");

    ServerPackManager::DownloadRequest wrong = matching;
    wrong.hash = std::string(40, 'c');
    const auto miss = cache.fetch(wrong);
    check(!miss.ok(), "a hash the cache does not hold is not answered from the cache");
    check(fs::exists(cache.cached_path(sha1_hex(bytes))),
          "and the file that did match is still there");

    // An http url nothing answers fails rather than hangs.
    ServerPackManager::DownloadRequest dead;
    dead.id = id_of(8);
    dead.url = "http://127.0.0.1:1/nothing.zip";
    DownloadLimits quick;
    quick.timeout_seconds = 2;
    quick.connect_timeout_seconds = 2;
    PackCache impatient(directory, quick);
    const auto failed = impatient.fetch(dead);
    check(!failed.ok(), "a host that is not listening fails");

    ServerPackManager::DownloadRequest bad_scheme;
    bad_scheme.id = id_of(8);
    bad_scheme.url = "file:///etc/passwd";
    check(!impatient.fetch(bad_scheme).ok(), "and a scheme we do not fetch is refused");

    fs::remove_all(directory, code);
}

void test_mipmaps() {
    // `ARGB.meanLinear`: four of the same texel average to that texel.
    Bitmap flat(2, 2);
    for (size_t i = 0; i + 3 < flat.rgba.size(); i += 4) {
        flat.rgba[i] = 128;
        flat.rgba[i + 1] = 64;
        flat.rgba[i + 2] = 32;
        flat.rgba[i + 3] = 255;
    }
    const Bitmap half = mean_linear(flat);
    check(half.width == 1 && half.height == 1, "a level is half as wide and half as tall");
    check(half.rgba[0] == 128 && half.rgba[1] == 64 && half.rgba[2] == 32 && half.rgba[3] == 255,
          "and four of one colour average back to it through the game's own tables");

    // A sixteen pixel sprite against a thirty-two pixel atlas: repeated up, so
    // every level below is the one the game would have put there.
    Bitmap sprite(16, 16);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            uint8_t* texel = sprite.at(x, y);
            texel[0] = static_cast<uint8_t>(x * 16);
            texel[1] = static_cast<uint8_t>(y * 16);
            texel[2] = 0;
            texel[3] = 255;
        }
    }
    const auto chain = chain_for(sprite, MipStrategy::Auto, 0.0f, 32, 6);
    check(chain.size() == 6, "an atlas of six levels gets six");
    bool sizes_right = true;
    for (uint32_t level = 0; level < chain.size(); ++level) {
        if (chain[level].size() != 1 || chain[level][0].width != (32u >> level)) {
            sizes_right = false;
        }
    }
    check(sizes_right, "and each is a square of the right side");
    check(chain[1][0].rgba == sprite.rgba,
          "the level matching the sprite's own size is the sprite itself, texel for texel");

    // A thirty-two pixel sprite needs no repeat at all.
    const auto native = chain_for(mcpp::packs::upscale(sprite, 2), MipStrategy::Mean, 0.0f, 32, 6);
    check(native.size() == 6 && native[0][0].width == 32, "a sprite at the atlas size fits");

    // A strip of four frames comes out as four frames per level.
    Bitmap strip(16, 64);
    for (size_t i = 3; i < strip.rgba.size(); i += 4) strip.rgba[i] = 255;
    check(frame_count(strip) == 4, "a strip four times as tall as it is wide has four frames");
    const auto animated = chain_for(strip, MipStrategy::Mean, 0.0f, 32, 6);
    check(animated[0].size() == 4, "and every level carries all four");

    // A cutout keeps its coverage: a sprite that is half transparent does not
    // thin away to nothing in the distance.
    Bitmap leaf(16, 16);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            uint8_t* texel = leaf.at(x, y);
            texel[0] = 40;
            texel[1] = 120;
            texel[2] = 40;
            texel[3] = ((x + y) % 2 == 0) ? 255 : 0;
        }
    }
    const auto levels = generate_mip_levels(leaf, 4, MipStrategy::Auto, 0.0f);
    check(levels.size() == 5, "a sixteen pixel cutout has five levels");
    const float first = alpha_test_coverage(levels[0], 0.5f, 1.0f);
    const float last = alpha_test_coverage(levels[2], 0.5f, 1.0f);
    check(first > 0.2f && last > first * 0.5f,
          "and the coverage the first level had is roughly held further down");
    bool solid = true;
    for (uint32_t y = 0; y < 16 && solid; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            const uint8_t* texel = levels[0].at(x, y);
            if (texel[0] == 0 && texel[1] == 0 && texel[2] == 0) solid = false;
        }
    }
    check(solid, "a cutout is solidified first, so no black bleeds into its edge");
}

// The whole apply path against the baked tables, with a target that only
// records. Skipped without the baked data.
void test_apply() {
    PackApplier applier;
    if (!applier.open(774)) {
        std::cout << "skipped: " << applier.error() << '\n';
        ++skipped;
        return;
    }
    check(applier.block_texture_count() > 1000, "the block table names its textures");
    check(applier.item_recipe_count() > 100,
          "and tools/bake_item_layers.py says which items are flat");

    check(PackApplier::texture_asset_path("minecraft:block/stone") ==
              "assets/minecraft/textures/block/stone.png",
          "a texture name becomes the path the bakers read it from");
    check(PackApplier::texture_name_of("assets/minecraft/textures/block/stone.png") ==
              "block/stone",
          "and back again");
    check(PackApplier::gui_sprite_name_of(
              "assets/minecraft/textures/gui/sprites/hud/hotbar.png") == "hud/hotbar",
          "a gui sprite path becomes the name the baker packed it under");
    check(PackApplier::gui_sprite_name_of(
              "assets/minecraft/textures/gui/container/furnace.png") == "container/furnace",
          "a container sheet keeps its own prefix");
    check(!PackApplier::gui_sprite_name_of("assets/minecraft/textures/block/stone.png")
               .has_value(),
          "and a block texture is not a gui sprite");

    RecordingTarget target(32, 6, 1886);
    target.items["diamond_sword"] = 936;
    target.rects["hud/hotbar"] = {64, 128, 182, 22};

    ZipWriter writer;
    writer.add_text("pack.mcmeta", kMeta);
    writer.add("assets/minecraft/textures/block/stone.png", flat_png(16, 0xFF00FFFFu), true);
    writer.add("assets/minecraft/textures/block/diamond_block.png", flat_png(32, 0x00FFFFFFu));
    writer.add("assets/minecraft/textures/item/diamond_sword.png", flat_png(16, 0xFFFF00FFu));
    writer.add("assets/minecraft/textures/gui/sprites/hud/hotbar.png", flat_png(16, 0x80FF80FFu));
    writer.add("assets/minecraft/textures/block/not_a_real_texture.png", flat_png(16, 0u));

    PackStack stack;
    check(stack.add_bytes(writer.finish()), "the test pack goes on the stack");
    const ApplyStats applied = applier.apply(stack, target);

    check(applied.block_textures == 2,
          "the two block textures the pack replaces are the two that land");
    check(applied.atlas_layers >= 2, "each takes at least its own layer");
    check(target.atlas.size() == applied.atlas_layers, "and the target was written once each");
    bool levels_right = !target.atlas.empty();
    for (const auto& [layer, levels] : target.atlas) {
        if (levels.size() != 6) levels_right = false;
        for (uint32_t mip = 0; mip < levels.size(); ++mip) {
            const size_t side = 32u >> mip;
            if (levels[mip].size() != side * side * 4) levels_right = false;
        }
        (void)layer;
    }
    check(levels_right, "every patched layer carries a whole mip chain at the atlas's sizes");

    // A sixteen pixel texture repeated up to the atlas's thirty-two: the top
    // level is the pack's colour everywhere.
    bool coloured = false;
    for (const auto& [layer, levels] : target.atlas) {
        if (levels.empty() || levels[0].size() < 4) continue;
        if (levels[0][0] == 0xFF && levels[0][1] == 0x00 && levels[0][2] == 0xFF) coloured = true;
        (void)layer;
    }
    check(coloured, "and the pack's own colour is what went in");

    check(applied.item_icons == 1 && target.icons.count(936) == 1,
          "the one item whose texture the pack replaces is composed again");
    const Bitmap& icon = target.icons[936];
    check(icon.width == 32 && icon.height == 32, "at the baked icon's own size");
    check(icon.rgba[0] == 0xFF && icon.rgba[1] == 0xFF && icon.rgba[2] == 0x00,
          "out of the pack's picture");

    check(applied.gui_sprites == 1 && target.sprites.count("hud/hotbar") == 1,
          "the interface sprite lands too");
    const Bitmap& sprite = target.sprites["hud/hotbar"];
    check(sprite.width == 182 && sprite.height == 22,
          "resampled to the rectangle it was packed into");

    check(applied.total_ms >= 0.0 && applied.mip_ms >= 0.0, "and the cost was measured");

    // Off again: every layer that was written is written back from the baked
    // tables, and nothing else is.
    const size_t writes_before = target.atlas_writes;
    const ApplyStats reverted = applier.revert(target);
    check(reverted.atlas_layers == applied.atlas_layers,
          "taking the pack off touches exactly the layers it put on");
    check(reverted.item_icons == 1 && reverted.gui_sprites == 1,
          "and the icon and the sprite as well");
    check(target.atlas_writes > writes_before, "the target saw the baked pixels go back");
    bool back_to_baked = true;
    for (const auto& [layer, levels] : target.atlas) {
        if (levels.empty() || levels[0].size() < 4) continue;
        if (levels[0][0] == 0xFF && levels[0][1] == 0x00 && levels[0][2] == 0xFF) {
            back_to_baked = false;
        }
        (void)layer;
    }
    check(back_to_baked, "and not one of them is still the pack's colour");
    check(!applier.anything_applied(), "nothing is left on");
}

// The packets, the download from the cache, the apply and the pop, through the
// one object a client talks to.
void test_system() {
    PackApplier probe;
    if (!probe.open(774)) {
        ++skipped;
        return;
    }
    const fs::path directory = fs::temp_directory_path() / "plasma-pack-system-test";
    std::error_code code;
    fs::remove_all(directory, code);

    RecordingTarget target(32, 6, 1886);
    target.items["diamond_sword"] = 936;
    target.rects["hud/hotbar"] = {64, 128, 182, 22};

    ZipWriter writer;
    writer.add_text("pack.mcmeta", kMeta);
    writer.add("assets/minecraft/textures/block/stone.png", flat_png(16, 0xFF00FFFFu), true);
    const std::vector<uint8_t> pack = writer.finish();

    PackSystem::Options options;
    options.cache = directory;
    options.prompt = PromptStatus::Pending;
    options.protocol = 774;
    PackSystem system(std::move(options), target);
    check(system.ready(), "the pack system opens the baked tables");

    std::vector<Action> sent;
    std::vector<std::string> prompts;
    bool disconnected = false;
    system.respond = [&](const PackId&, Action action) { sent.push_back(action); };
    system.disconnect = [&](std::string_view) { disconnected = true; };
    system.show_prompt = [&](const PackSystem::Prompt& prompt) { prompts.push_back(prompt.title); };

    // A pack already in the cache: the url is never reached, so the whole path
    // runs without a server.
    PackCache cache(directory);
    const auto stored = cache.store(id_of(11), pack);
    check(stored.ok(), "the test pack is put in the cache");

    Push push;
    push.id = id_of(11);
    push.url = "http://127.0.0.1:1/pack.zip";
    push.hash = sha1_hex(pack);
    push.required = false;
    system.push(push);
    check(prompts.size() == 1 && sent.empty(),
          "a pending prompt asks the player and answers nothing yet");

    system.allow();
    system.tick();
    check(sent.size() == 3 && sent[0] == Action::Accepted && sent[1] == Action::Downloaded &&
              sent[2] == Action::SuccessfullyLoaded,
          "yes runs accepted, downloaded and successfully_loaded in that order");
    check(system.applied_packs() == 1, "and one pack is on");
    check(system.last_apply().block_textures == 1, "with the texture it replaces");
    check(!target.atlas.empty(), "which reached the target");

    sent.clear();
    system.pop(Pop{});
    system.tick();
    check(system.applied_packs() == 0, "popping every pack takes it off");
    check(system.last_revert().atlas_layers >= 1, "and puts the baked layers back");

    // A required pack declined: the connection has to go.
    PackSystem::Options strict;
    strict.cache = directory;
    strict.prompt = PromptStatus::Pending;
    strict.protocol = 774;
    PackSystem required(std::move(strict), target);
    bool went = false;
    required.disconnect = [&](std::string_view) { went = true; };
    required.respond = [](const PackId&, Action) {};
    required.show_prompt = [](const PackSystem::Prompt&) {};
    Push must;
    must.id = id_of(12);
    must.url = "http://127.0.0.1:1/pack.zip";
    must.required = true;
    required.push(must);
    required.reject();
    required.tick();
    check(went, "declining a required pack disconnects the player");
    check(!disconnected, "and declining a recommended one does not");

    // A url we do not fetch is answered invalid_url without a prompt.
    sent.clear();
    prompts.clear();
    Push bad;
    bad.id = id_of(13);
    bad.url = "file:///etc/passwd";
    system.push(bad);
    check(sent.size() == 1 && sent[0] == Action::InvalidUrl && prompts.empty(),
          "a url that is not http or https is refused outright");

    // A hash the file does not have: the pack never lands.
    sent.clear();
    Push wrong;
    wrong.id = id_of(14);
    wrong.url = "http://127.0.0.1:1/pack.zip";
    wrong.hash = std::string(40, 'd');
    system.push(wrong);
    system.tick();
    check(!sent.empty() && sent.back() == Action::FailedDownload,
          "a pack whose hash does not match is not applied");
    check(system.applied_packs() == 0, "and nothing is on");

    fs::remove_all(directory, code);
}

} // namespace

int main() {
    test_protocol_values();
    test_state_machine();
    test_declining();
    test_download_failure();
    test_two_packs_stack();
    test_zip();
    test_pack_metadata();
    test_stack_order();
    test_hashing_and_cache();
    test_mipmaps();
    test_apply();
    test_system();
    if (skipped > 0) {
        std::cout << std::format("{} test(s) skipped: no baked data in "
                                 "~/.local/share/plasma-client/data\n", skipped);
    }
    std::cout << (failures == 0 ? "packs: ok\n" : std::format("packs: {} failures\n", failures));
    return failures == 0 ? 0 : 1;
}
