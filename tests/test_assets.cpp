#include "mcpp/assets/object_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void write(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

constexpr std::string_view kClick = "OggS not really";
constexpr std::string_view kClickHash = "0123456789abcdef0123456789abcdef01234567";
constexpr std::string_view kLangHash = "fedcba9876543210fedcba9876543210fedcba98";
constexpr std::string_view kShortHash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

// A launcher directory the way the vanilla launcher lays one out, with one
// version, its index, two objects present, one cut short and one never fetched.
fs::path fake_launcher() {
    const fs::path root = fs::temp_directory_path() / "plasma-test-assets";
    fs::remove_all(root);
    write(root / "versions/9.9/9.9.json", R"({"id": "9.9", "assetIndex": {"id": "77"}})");
    write(root / "versions/9.9-0.1.0/9.9-0.1.0.json", R"({"assetIndex": {"id": "wrong"}})");
    write(root / "versions/8.8-0.2.0/8.8-0.2.0.json", R"({"assetIndex": {"id": "88"}})");
    write(root / "assets/indexes/77.json", std::format(R"({{"objects": {{
        "minecraft/sounds/random/click.ogg": {{"hash": "{}", "size": {}}},
        "minecraft/lang/nl_nl.json": {{"hash": "{}", "size": 2}},
        "minecraft/sounds/random/short.ogg": {{"hash": "{}", "size": 999}},
        "minecraft/sounds/random/never.ogg": {{"hash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "size": 1}},
        "minecraft/sounds/../../escape": {{"hash": "../../../../etc/passwd", "size": 1}}
    }}}})", kClickHash, kClick.size(), kLangHash, kShortHash));
    write(root / "assets/objects/01" / kClickHash, kClick);
    write(root / "assets/objects/fe" / kLangHash, "{}");
    write(root / "assets/objects/aa" / kShortHash, "cut");
    return root;
}

void test_index_lookup(const fs::path& root) {
    using mcpp::assets::ObjectStore;
    check(ObjectStore::index_for_version(root, "9.9") == "77",
          "the vanilla version json names the index");
    check(ObjectStore::index_for_version(root, "8.8") == "88",
          "a loader's directory is found by its prefix");
    check(ObjectStore::index_for_version(root, "9").empty(),
          "a version that only prefixes another is not that version");
    check(ObjectStore::index_for_version(root, "7.7").empty(), "an unknown version has no index");
}

void test_objects(const fs::path& root) {
    mcpp::assets::ObjectStore store;
    check(!store.open(root / "assets", "76"), "an index that is not there does not open");
    check(store.open(root / "assets", "77"), "the index opens");
    check(store.size() == 4, std::format("four usable entries, the bad hash left out ({})",
                                         store.size()));

    const auto click = store.path("minecraft/sounds/random/click.ogg");
    check(click == root / "assets/objects/01" / kClickHash,
          "a name resolves to objects/<first two>/<sha1>");
    const auto bytes = store.read("minecraft/sounds/random/click.ogg");
    check(bytes && std::string(bytes->begin(), bytes->end()) == kClick, "the bytes come back");

    check(store.path("minecraft/sounds/random/never.ogg").empty(),
          "an object never downloaded has no path");
    check(!store.read("minecraft/sounds/random/short.ogg"),
          "an object shorter than the index says is not read");
    check(store.path("minecraft/sounds/nothing.ogg").empty(), "an unknown name has no path");
    check(store.find("minecraft/sounds/../../escape") == nullptr,
          "a hash that is not a sha1 is never turned into a path");

    const auto sounds = store.names("minecraft/sounds/");
    check(sounds.size() == 3 && sounds.front() == "minecraft/sounds/random/click.ogg",
          "names under a prefix, sorted");
}

void test_search(const fs::path& root) {
    ::setenv("PLASMA_MINECRAFT", root.c_str(), 1);
    const auto store = mcpp::assets::ObjectStore::for_version("9.9");
    check(store.has_value() && store->directory() == root / "assets",
          "a chosen directory is searched first");
    check(store && store->read("minecraft/lang/nl_nl.json").has_value(),
          "and reads from that directory");
    ::unsetenv("PLASMA_MINECRAFT");
}

} // namespace

int main() {
    const auto root = fake_launcher();
    test_index_lookup(root);
    test_objects(root);
    test_search(root);
    fs::remove_all(root);

    if (failures == 0) {
        std::cout << "all asset tests passed\n";
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
