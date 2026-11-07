#pragma once
#include "mcpp/packs/pack_apply.hpp"
#include "mcpp/packs/pack_cache.hpp"
#include "mcpp/packs/pack_protocol.hpp"
#include "mcpp/packs/resource_pack.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mcpp::gpu {
class UiRenderer;
class WorldRenderer;
} // namespace mcpp::gpu

// The whole server-pack path in one place: the packets in, the state machine,
// the download, the zip, and the pack on top of the baked tables. A client
// hands it a push and a pop and ticks it; it hands back the answers to send.
namespace mcpp::packs {

// `PackTarget` over the two renderers that hold the baked pixels.
class GpuPackTarget : public PackTarget {
public:
    GpuPackTarget(gpu::WorldRenderer* world, gpu::UiRenderer* ui) : world_(world), ui_(ui) {}

    uint32_t atlas_tile() const override;
    uint32_t atlas_mips() const override;
    uint32_t atlas_layers() const override;
    double patch_atlas(std::span<const LayerPatch> patches) override;

    uint32_t item_tile() const override;
    int32_t item_layer_of(std::string_view name) const override;
    bool set_item_icon(int32_t layer, const Bitmap& icon) override;

    std::array<uint32_t, 4> sprite_rect(std::string_view name) const override;
    bool set_sprite_pixels(std::string_view name, const Bitmap& pixels) override;

private:
    gpu::WorldRenderer* world_ = nullptr;
    gpu::UiRenderer* ui_ = nullptr;
};

class PackSystem {
public:
    struct Options {
        // `DownloadedPackSource`'s own directory under the game folder.
        std::filesystem::path cache;
        // `Options.serverResourcePacks`.
        PromptStatus prompt = PromptStatus::Pending;
        int32_t protocol = 774;
        PackCache::Identity identity;
        ZipLimits zip;
        DownloadLimits download;
    };

    PackSystem(Options options, PackTarget& target);

    // What the client must do with the answers: send this action for this
    // pack. Set before the first push.
    std::function<void(const PackId&, Action)> respond;
    // A required pack the player declined: the connection has to go.
    std::function<void(std::string_view)> disconnect;
    // One line per step, for the log and for the tests.
    std::function<void(std::string_view)> status;
    // `ClientCommonPacketListenerImpl.addOrUpdatePackPrompt`: the two lines
    // vanilla shows, and the server's own line under them. Without a handler
    // the prompt is left pending and nothing is fetched until the player
    // answers through allow() or reject().
    struct Prompt {
        std::vector<Push> requests;
        bool required = false;
        std::optional<std::string> server_line;
        std::string title;
        std::string body;
        std::string yes;
        std::string no;
    };
    std::function<void(const Prompt&)> show_prompt;

    // `ClientCommonPacketListenerImpl.handleResourcePackPush` / `...Pop`.
    void push(const Push& push);
    // `ServerPackManager.pushLocalPack`: a pack already on disk, which skips
    // the prompt and the download. What a local world uses, and what the
    // offscreen proof uses.
    void push_local(const PackId& id, const std::filesystem::path& path);
    void pop(const Pop& pop);
    // Downloads run on the calling thread inside tick, so a client that does
    // not want to block a frame calls it off the render thread.
    void tick();
    // The prompt's two answers.
    void allow();
    void reject();
    // `DownloadedPackSource.cleanupAfterDisconnect`.
    void leave();

    const ApplyStats& last_apply() const { return last_apply_; }
    const ApplyStats& last_revert() const { return last_revert_; }
    const std::vector<PackCache::Result>& last_downloads() const { return downloads_; }
    size_t applied_packs() const { return stack_.size(); }
    bool ready() const { return applier_.is_open(); }
    const std::string& error() const { return error_; }
    ServerPackManager& manager() { return manager_; }

private:
    ServerPackManager::Hooks make_hooks();
    void on_download(std::vector<ServerPackManager::DownloadRequest> batch);
    void on_reload(std::vector<ServerPackManager::IdAndPath> packs);

    Options options_;
    PackTarget& target_;
    PackCache cache_;
    PackApplier applier_;
    PackStack stack_;
    ServerPackManager manager_;
    Prompt prompt_;
    bool prompt_open_ = false;
    ApplyStats last_apply_;
    ApplyStats last_revert_;
    std::vector<PackCache::Result> downloads_;
    std::vector<ServerPackManager::BatchResult> pending_;
    std::string error_;
};

// The directory vanilla keeps downloaded packs in.
std::filesystem::path default_pack_cache();

} // namespace mcpp::packs
