#include "mcpp/packs/pack_system.hpp"

#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/world_renderer.hpp"

#include <cstdlib>
#include <format>

namespace mcpp::packs {

uint32_t GpuPackTarget::atlas_tile() const { return world_ != nullptr ? world_->atlas_tile() : 0; }
uint32_t GpuPackTarget::atlas_mips() const { return world_ != nullptr ? world_->atlas_mips() : 0; }

uint32_t GpuPackTarget::atlas_layers() const {
    return world_ != nullptr ? world_->atlas_layers() : 0;
}

double GpuPackTarget::patch_atlas(std::span<const LayerPatch> patches) {
    if (world_ == nullptr) return 0.0;
    std::vector<gpu::WorldRenderer::AtlasLayerPatch> out;
    out.reserve(patches.size());
    for (const LayerPatch& patch : patches) {
        out.push_back(gpu::WorldRenderer::AtlasLayerPatch{patch.layer, patch.levels});
    }
    return world_->patch_atlas(out);
}

uint32_t GpuPackTarget::item_tile() const { return ui_ != nullptr ? ui_->item_tile() : 0; }

int32_t GpuPackTarget::item_layer_of(std::string_view name) const {
    return ui_ != nullptr ? ui_->item_layer_of(name) : -1;
}

bool GpuPackTarget::set_item_icon(int32_t layer, const Bitmap& icon) {
    return ui_ != nullptr && ui_->set_item_icon(layer, icon.rgba, icon.width, icon.height);
}

std::array<uint32_t, 4> GpuPackTarget::sprite_rect(std::string_view name) const {
    if (ui_ == nullptr) return {0, 0, 0, 0};
    return ui_->sprite_rect(name);
}

bool GpuPackTarget::set_sprite_pixels(std::string_view name, const Bitmap& pixels) {
    return ui_ != nullptr && ui_->set_sprite_pixels(name, pixels.rgba, pixels.width, pixels.height);
}

std::filesystem::path default_pack_cache() {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "server-resource-packs";
}

ServerPackManager::Hooks PackSystem::make_hooks() {
    ServerPackManager::Hooks hooks;
    hooks.download = [this](std::vector<ServerPackManager::DownloadRequest> batch) {
        on_download(std::move(batch));
    };
    hooks.schedule_reload = [this](std::vector<ServerPackManager::IdAndPath> packs) {
        on_reload(std::move(packs));
    };
    hooks.respond = [this](const PackId& id, Action action) {
        if (status) status(std::format("pack {}: {}", id_to_string(id), action_name(action)));
        if (respond) respond(id, action);
    };
    return hooks;
}

PackSystem::PackSystem(Options options, PackTarget& target)
    : options_(std::move(options)), target_(target),
      cache_(options_.cache.empty() ? default_pack_cache() : options_.cache, options_.download),
      manager_(make_hooks(), options_.prompt) {
    cache_.set_identity(options_.identity);
    cache_.vacuum();
    if (!applier_.open(options_.protocol)) error_ = applier_.error();
}

// `ClientCommonPacketListenerImpl.handleResourcePackPush`: a url that is not
// http or https is refused outright, and the prompt stands in front of the
// fetch unless the player has already said always or never for this server.
void PackSystem::push(const Push& request) {
    if (!pack_url_allowed(request.url)) {
        if (respond) respond(request.id, Action::InvalidUrl);
        if (status) status(std::format("pack {} has a url we do not fetch", request.url));
        return;
    }
    const auto hash = parse_sha1(request.hash);
    const PromptStatus decided = manager_.prompt_status();
    const bool prompt_needed =
        decided == PromptStatus::Pending ||
        (request.required && decided == PromptStatus::Declined);
    if (!prompt_needed) {
        manager_.push_pack(request.id, request.url, hash);
        manager_.run_updates();
        return;
    }
    if (!prompt_open_) {
        prompt_ = Prompt{};
        prompt_open_ = true;
    }
    prompt_.requests.push_back(request);
    prompt_.required = request.required;
    prompt_.server_line = request.prompt;
    // The two lines vanilla shows, by their translation keys' English text.
    prompt_.title = request.required ? "This server requires a custom resource pack."
                                     : "This server recommends the use of a custom resource pack.";
    prompt_.body = request.required
                       ? "Rejecting this custom resource pack will disconnect you from this server."
                       : "Do you want to download and install it automatically?";
    prompt_.yes = request.required ? "Proceed" : "Yes";
    prompt_.no = request.required ? "Disconnect" : "No";
    if (show_prompt) show_prompt(prompt_);
}

void PackSystem::push_local(const PackId& id, const std::filesystem::path& path) {
    manager_.allow_server_packs();
    manager_.push_local_pack(id, path);
    manager_.run_updates();
}

void PackSystem::pop(const Pop& request) {
    if (request.id.has_value()) {
        manager_.pop_pack(*request.id);
    } else {
        manager_.pop_all();
    }
    manager_.run_updates();
}

// `ClientCommonPacketListenerImpl.PackConfirmScreen`: yes remembers always for
// this server and pushes everything that was waiting.
void PackSystem::allow() {
    const Prompt prompt = prompt_;
    prompt_ = Prompt{};
    prompt_open_ = false;
    manager_.allow_server_packs();
    for (const Push& request : prompt.requests) {
        manager_.push_pack(request.id, request.url, parse_sha1(request.hash));
    }
    manager_.run_updates();
}

void PackSystem::reject() {
    const Prompt prompt = prompt_;
    prompt_ = Prompt{};
    prompt_open_ = false;
    manager_.reject_server_packs();
    if (prompt.required && disconnect) {
        disconnect("Disconnected: this server requires a custom resource pack");
    }
    for (const Push& request : prompt.requests) {
        manager_.push_pack(request.id, request.url, parse_sha1(request.hash));
    }
    manager_.run_updates();
}

void PackSystem::leave() {
    manager_.cleanup_after_disconnect();
    manager_.run_updates();
    if (applier_.anything_applied()) last_revert_ = applier_.revert(target_);
    stack_.clear();
    prompt_ = Prompt{};
    prompt_open_ = false;
}

// `DownloadQueue.runDownload`: one batch, each pack fetched or counted failed.
void PackSystem::on_download(std::vector<ServerPackManager::DownloadRequest> batch) {
    ServerPackManager::BatchResult result;
    downloads_.clear();
    for (const auto& request : batch) {
        PackCache::Result one = cache_.fetch(request);
        if (one.ok()) {
            result.downloaded.emplace_back(one.id, one.path);
            if (status) {
                status(std::format("pack {} {} in {:.1f} ms ({} bytes)", id_to_string(one.id),
                                   one.from_cache ? "came from the cache" : "downloaded",
                                   one.download_ms + one.hash_ms, one.bytes));
            }
        } else {
            result.failed.push_back(one.id);
            if (status) status(std::format("pack {}: {}", id_to_string(one.id), one.error));
        }
        downloads_.push_back(std::move(one));
    }
    pending_.push_back(std::move(result));
}

// `DownloadedPackSource.startReload`: open every pack in the stack and put it
// on top of the baked tables. A pack that will not open fails the whole
// reload, the way an unreadable pack.mcmeta does in vanilla.
void PackSystem::on_reload(std::vector<ServerPackManager::IdAndPath> packs) {
    if (applier_.anything_applied()) last_revert_ = applier_.revert(target_);
    stack_.clear();
    for (const auto& pack : packs) {
        if (!stack_.add(pack.path, options_.zip)) {
            error_ = stack_.error();
            if (status) status(std::format("pack {} will not open: {}", id_to_string(pack.id),
                                           error_));
            stack_.clear();
            manager_.reload_failed(true);
            return;
        }
    }
    if (!stack_.empty()) {
        last_apply_ = applier_.apply(stack_, target_);
        if (status) {
            status(std::format("{} pack(s) applied: {} block textures over {} atlas layers, "
                               "{} item icons, {} interface sprites, {} skipped, "
                               "{:.1f} ms (read {:.1f}, mips {:.1f}, upload {:.1f})",
                               stack_.size(), last_apply_.block_textures, last_apply_.atlas_layers,
                               last_apply_.item_icons, last_apply_.gui_sprites,
                               last_apply_.skipped, last_apply_.total_ms, last_apply_.read_ms,
                               last_apply_.mip_ms, last_apply_.upload_ms));
        }
    }
    manager_.reload_succeeded();
}

void PackSystem::tick() {
    for (const auto& result : pending_) manager_.deliver_download(result);
    pending_.clear();
    manager_.run_updates();
}

} // namespace mcpp::packs
