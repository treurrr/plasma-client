#include "mcpp/packs/pack_protocol.hpp"

#include <algorithm>
#include <format>

namespace mcpp::packs {
namespace {

constexpr char kHex[] = "0123456789abcdef";

bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

} // namespace

std::string id_to_string(const PackId& id) {
    std::string out;
    out.reserve(36);
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHex[id[i] >> 4]);
        out.push_back(kHex[id[i] & 0xF]);
    }
    return out;
}

std::string_view action_name(Action action) {
    switch (action) {
    case Action::SuccessfullyLoaded: return "successfully_loaded";
    case Action::Declined: return "declined";
    case Action::FailedDownload: return "failed_download";
    case Action::Accepted: return "accepted";
    case Action::Downloaded: return "downloaded";
    case Action::InvalidUrl: return "invalid_url";
    case Action::FailedReload: return "failed_reload";
    case Action::Discarded: return "discarded";
    }
    return "?";
}

PromptStatus prompt_status_from_option(std::string_view value) {
    if (value == "enabled" || value == "always") return PromptStatus::Allowed;
    if (value == "disabled" || value == "never") return PromptStatus::Declined;
    return PromptStatus::Pending;
}

std::string_view prompt_status_option(PromptStatus status) {
    switch (status) {
    case PromptStatus::Allowed: return "enabled";
    case PromptStatus::Declined: return "disabled";
    case PromptStatus::Pending: break;
    }
    return "prompt";
}

// `DownloadedPackSource.createPackResponseSender`: each step of the pack's
// life is one action on the wire.
Action action_of(Update update) {
    switch (update) {
    case Update::Accepted: return Action::Accepted;
    case Update::Downloaded: return Action::Downloaded;
    }
    return Action::Accepted;
}

Action action_of(FinalResult result) {
    switch (result) {
    case FinalResult::Applied: return Action::SuccessfullyLoaded;
    case FinalResult::DownloadFailed: return Action::FailedDownload;
    case FinalResult::Declined: return Action::Declined;
    case FinalResult::Discarded: return Action::Discarded;
    case FinalResult::ActivationFailed: return Action::FailedReload;
    }
    return Action::Discarded;
}

// `ClientCommonPacketListenerImpl.parseResourcePackUrl`: a scheme other than
// http or https is not fetched at all.
bool pack_url_allowed(std::string_view url) {
    const size_t colon = url.find("://");
    if (colon == std::string_view::npos) return false;
    std::string scheme;
    for (size_t i = 0; i < colon; ++i) scheme.push_back(lower(url[i]));
    if (scheme != "http" && scheme != "https") return false;
    return colon + 3 < url.size();
}

// `DownloadedPackSource.tryParseSha1Hash`.
std::optional<std::string> parse_sha1(std::string_view hash) {
    if (hash.size() != 40) return std::nullopt;
    std::string out;
    out.reserve(40);
    for (const char c : hash) {
        if (!is_hex(c)) return std::nullopt;
        out.push_back(lower(c));
    }
    return out;
}

ServerPackManager::ServerPackManager(Hooks hooks, PromptStatus status)
    : hooks_(std::move(hooks)), prompt_status_(status) {}

// `ServerPackManager.RemovalReason`: two of the six are the server's own doing
// and need no answer, because the server already knows.
std::optional<FinalResult> ServerPackManager::server_response(RemovalReason reason) {
    switch (reason) {
    case RemovalReason::DownloadFailed: return FinalResult::DownloadFailed;
    case RemovalReason::ActivationFailed: return FinalResult::ActivationFailed;
    case RemovalReason::Declined: return FinalResult::Declined;
    case RemovalReason::Discarded: return FinalResult::Discarded;
    case RemovalReason::ServerRemoved:
    case RemovalReason::ServerReplaced: return std::nullopt;
    }
    return std::nullopt;
}

void ServerPackManager::report_update(const PackId& id, Update update) {
    if (hooks_.respond) hooks_.respond(id, action_of(update));
}

void ServerPackManager::report_final_result(const PackId& id, FinalResult result) {
    if (hooks_.respond) hooks_.respond(id, action_of(result));
}

// `ServerPackManager.registerForUpdate`.
void ServerPackManager::register_for_update() { has_updates_ = true; }

// `DownloadedPackSource.createUpdateScheduler.runAllUpdates`.
void ServerPackManager::run_updates() {
    if (running_updates_) return;
    running_updates_ = true;
    while (has_updates_) {
        has_updates_ = false;
        tick();
    }
    running_updates_ = false;
}

// `ServerPackManager.markExistingPacksAsRemoved`.
void ServerPackManager::mark_existing_packs_as_removed(const PackId& id) {
    for (PackData& pack : packs_) {
        if (pack.id == id) pack.set_removal_reason_if_not_set(RemovalReason::ServerReplaced);
    }
}

// `ServerPackManager.pushPack`.
void ServerPackManager::push_pack(const PackId& id, std::string url,
                                  std::optional<std::string> hash) {
    if (prompt_status_ == PromptStatus::Declined) {
        report_final_result(id, FinalResult::Declined);
        return;
    }
    PackData pack;
    pack.id = id;
    pack.url = std::move(url);
    pack.hash = std::move(hash);
    push_new_pack(std::move(pack));
}

// `ServerPackManager.pushLocalPack`.
void ServerPackManager::push_local_pack(const PackId& id, std::filesystem::path path) {
    if (prompt_status_ == PromptStatus::Declined) {
        report_final_result(id, FinalResult::Declined);
        return;
    }
    PackData pack;
    pack.id = id;
    pack.download_status = DownloadStatus::Done;
    pack.path = std::move(path);
    push_new_pack(std::move(pack));
}

// `ServerPackManager.pushNewPack`.
void ServerPackManager::push_new_pack(PackData pack) {
    mark_existing_packs_as_removed(pack.id);
    packs_.push_back(std::move(pack));
    if (prompt_status_ == PromptStatus::Allowed) accept_pack(packs_.back());
    register_for_update();
}

// `ServerPackManager.acceptPack`: the first answer the server gets.
void ServerPackManager::accept_pack(PackData& pack) {
    report_update(pack.id, Update::Accepted);
    pack.prompt_accepted = true;
}

// `ServerPackManager.findPackInfo`.
ServerPackManager::PackData* ServerPackManager::find_pack_info(const PackId& id) {
    for (PackData& pack : packs_) {
        if (!pack.removed() && pack.id == id) return &pack;
    }
    return nullptr;
}

// `ServerPackManager.popPack`.
void ServerPackManager::pop_pack(const PackId& id) {
    if (PackData* pack = find_pack_info(id); pack != nullptr) {
        pack->set_removal_reason_if_not_set(RemovalReason::ServerRemoved);
        register_for_update();
    }
}

// `ServerPackManager.popAll`.
void ServerPackManager::pop_all() {
    for (PackData& pack : packs_) {
        pack.set_removal_reason_if_not_set(RemovalReason::ServerRemoved);
    }
    register_for_update();
}

// `ServerPackManager.allowServerPacks`.
void ServerPackManager::allow_server_packs() {
    prompt_status_ = PromptStatus::Allowed;
    for (PackData& pack : packs_) {
        if (!pack.prompt_accepted && !pack.removed()) accept_pack(pack);
    }
    register_for_update();
}

// `ServerPackManager.rejectServerPacks`.
void ServerPackManager::reject_server_packs() {
    prompt_status_ = PromptStatus::Declined;
    for (PackData& pack : packs_) {
        if (!pack.prompt_accepted) pack.set_removal_reason_if_not_set(RemovalReason::Declined);
    }
    register_for_update();
}

// `ServerPackManager.resetPromptStatus`.
void ServerPackManager::reset_prompt_status() { prompt_status_ = PromptStatus::Pending; }

// `DownloadedPackSource.cleanupAfterDisconnect`.
void ServerPackManager::cleanup_after_disconnect() {
    pop_all();
    hooks_.respond = nullptr;
    reset_prompt_status();
}

// `ServerPackManager.tick`.
void ServerPackManager::tick() {
    if (!update_downloads()) trigger_reload_if_needed();
    cleanup_removed_packs();
}

// `ServerPackManager.cleanupRemovedPacks`: a pack is forgotten only once it is
// off screen and the server has been told why.
void ServerPackManager::cleanup_removed_packs() {
    std::erase_if(packs_, [&](PackData& pack) {
        if (pack.activation_status != ActivationStatus::Inactive) return false;
        if (!pack.removal_reason.has_value()) return false;
        if (const auto response = server_response(*pack.removal_reason)) {
            report_final_result(pack.id, *response);
        }
        return true;
    });
}

// `ServerPackManager.updateDownloads`: true while anything accepted has still
// not arrived, which is what holds the reload back.
bool ServerPackManager::update_downloads() {
    std::vector<DownloadRequest> batch;
    bool waiting = false;
    for (PackData& pack : packs_) {
        if (pack.removed() || !pack.prompt_accepted) continue;
        if (pack.download_status != DownloadStatus::Done) waiting = true;
        if (pack.download_status == DownloadStatus::Requested) {
            pack.download_status = DownloadStatus::Pending;
            batch.push_back(DownloadRequest{pack.id, pack.url, pack.hash});
        }
    }
    if (!batch.empty() && hooks_.download) hooks_.download(std::move(batch));
    return waiting;
}

// `ServerPackManager.onDownload`: one failure in a batch discards the whole
// batch, because a half-applied stack is not the look the server asked for.
void ServerPackManager::deliver_download(const BatchResult& result) {
    if (!result.failed.empty()) {
        for (PackData& pack : packs_) {
            if (pack.activation_status == ActivationStatus::Active) continue;
            const bool failed = std::find(result.failed.begin(), result.failed.end(), pack.id) !=
                                result.failed.end();
            pack.set_removal_reason_if_not_set(failed ? RemovalReason::DownloadFailed
                                                      : RemovalReason::Discarded);
        }
    }
    for (const auto& [id, path] : result.downloaded) {
        PackData* pack = nullptr;
        for (PackData& candidate : packs_) {
            if (candidate.id == id) pack = &candidate;
        }
        if (pack == nullptr) continue;
        pack->download_status = DownloadStatus::Done;
        pack->path = path;
        if (!pack->removed()) report_update(pack->id, Update::Downloaded);
    }
    register_for_update();
}

// `ServerPackManager.triggerReloadIfNeeded`.
void ServerPackManager::trigger_reload_if_needed() {
    bool changed = false;
    std::vector<PackId> to_load;
    std::vector<PackId> to_unload;
    for (const PackData& pack : packs_) {
        if (pack.activation_status == ActivationStatus::Pending) return;
        const bool wanted = pack.prompt_accepted &&
                            pack.download_status == DownloadStatus::Done && !pack.removed();
        if (wanted && pack.activation_status == ActivationStatus::Inactive) {
            to_load.push_back(pack.id);
            changed = true;
        }
        if (pack.activation_status == ActivationStatus::Active) {
            if (!wanted) {
                changed = true;
                to_unload.push_back(pack.id);
            } else {
                to_load.push_back(pack.id);
            }
        }
    }
    if (!changed) return;

    for (PackData& pack : packs_) {
        const bool loading = std::find(to_load.begin(), to_load.end(), pack.id) != to_load.end();
        const bool unloading =
            std::find(to_unload.begin(), to_unload.end(), pack.id) != to_unload.end();
        if (loading && pack.activation_status != ActivationStatus::Active) {
            pack.activation_status = ActivationStatus::Pending;
        }
        if (unloading) pack.activation_status = ActivationStatus::Pending;
    }

    reload_to_activate_ = to_load;
    reload_to_deactivate_ = to_unload;
    reload_pending_ = true;
    if (hooks_.schedule_reload) hooks_.schedule_reload(packs_to_load());
}

// `PackReloadConfig.Callbacks.packsToLoad`: the stack as it will be shown,
// the pack the server pushed first at the bottom.
std::vector<ServerPackManager::IdAndPath> ServerPackManager::packs_to_load() const {
    std::vector<IdAndPath> out;
    out.reserve(reload_to_activate_.size());
    for (const PackId& id : reload_to_activate_) {
        for (const PackData& pack : packs_) {
            if (pack.id == id) out.push_back(IdAndPath{pack.id, pack.path});
        }
    }
    return out;
}

std::vector<ServerPackManager::IdAndPath> ServerPackManager::applied() const {
    std::vector<IdAndPath> out;
    for (const PackData& pack : packs_) {
        if (pack.activation_status == ActivationStatus::Active) {
            out.push_back(IdAndPath{pack.id, pack.path});
        }
    }
    return out;
}

// `PackReloadConfig.Callbacks.onSuccess`.
void ServerPackManager::reload_succeeded() {
    if (!reload_pending_) return;
    reload_pending_ = false;
    for (const PackId& id : reload_to_activate_) {
        for (PackData& pack : packs_) {
            if (pack.id != id) continue;
            pack.activation_status = ActivationStatus::Active;
            if (!pack.removal_reason.has_value()) {
                report_final_result(pack.id, FinalResult::Applied);
            }
        }
    }
    for (const PackId& id : reload_to_deactivate_) {
        for (PackData& pack : packs_) {
            if (pack.id == id) pack.activation_status = ActivationStatus::Inactive;
        }
    }
    reload_to_activate_.clear();
    reload_to_deactivate_.clear();
    register_for_update();
}

// `PackReloadConfig.Callbacks.onFailure`: give_up is the game's recovery
// having failed too, and then nothing is retried.
void ServerPackManager::reload_failed(bool give_up) {
    if (!reload_pending_) return;
    if (give_up) {
        reload_pending_ = false;
        for (PackData& pack : packs_) {
            if (pack.activation_status == ActivationStatus::Pending) {
                pack.activation_status = ActivationStatus::Inactive;
            }
        }
        reload_to_activate_.clear();
        reload_to_deactivate_.clear();
        return;
    }
    reload_to_activate_.clear();
    for (PackData& pack : packs_) {
        switch (pack.activation_status) {
        case ActivationStatus::Inactive:
            pack.set_removal_reason_if_not_set(RemovalReason::Discarded);
            break;
        case ActivationStatus::Pending:
            pack.activation_status = ActivationStatus::Inactive;
            pack.set_removal_reason_if_not_set(RemovalReason::ActivationFailed);
            break;
        case ActivationStatus::Active:
            reload_to_activate_.push_back(pack.id);
            break;
        }
    }
    register_for_update();
}

} // namespace mcpp::packs
