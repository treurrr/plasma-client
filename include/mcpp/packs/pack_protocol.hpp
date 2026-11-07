#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The server's own resource pack: the packets that push and pop one, the
// answers the player sends back, and the order several of them stack in.
// Everything here is the game's bookkeeping, not ours: a pack is accepted,
// downloaded and applied in three separate steps, and the server is told
// about each of them.
namespace mcpp::packs {

using PackId = std::array<uint8_t, 16>;

std::string id_to_string(const PackId& id);

// `ServerboundResourcePackPacket.Action`: everything the player can answer.
// The two that are not terminal are progress, not an answer: the server keeps
// waiting after them.
enum class Action : int32_t {
    SuccessfullyLoaded = 0,
    Declined = 1,
    FailedDownload = 2,
    Accepted = 3,
    Downloaded = 4,
    InvalidUrl = 5,
    FailedReload = 6,
    Discarded = 7,
};

// `ServerboundResourcePackPacket.Action.isTerminal`: the server stops waiting.
constexpr bool is_terminal(Action action) {
    return action != Action::Accepted && action != Action::Downloaded;
}

std::string_view action_name(Action action);

// `ClientboundResourcePackPushPacket`: the id to answer about, where to fetch
// it, the sha1 the file must have, whether declining disconnects us, and the
// line the server wants shown above the prompt.
struct Push {
    PackId id{};
    std::string url;
    std::string hash;
    bool required = false;
    std::optional<std::string> prompt;
};
// `ClientboundResourcePackPushPacket.MAX_HASH_LENGTH`.
constexpr size_t kMaxHashLength = 40;

// `ClientboundResourcePackPopPacket`: one pack, or every pack when the
// optional id is absent.
struct Pop {
    std::optional<PackId> id;
};

// `ServerData.ServerPackStatus` as the option file spells it: what the player
// has already decided for this server, before any prompt.
enum class PromptStatus { Pending, Allowed, Declined };

// `Options.serverResourcePacks`: prompt, always or never, the three the
// vanilla options screen offers.
PromptStatus prompt_status_from_option(std::string_view value);
std::string_view prompt_status_option(PromptStatus status);

// `PackLoadFeedback.Update` and `.FinalResult`: progress, then the one answer
// that ends the exchange.
enum class Update { Accepted, Downloaded };
enum class FinalResult { Declined, Applied, Discarded, DownloadFailed, ActivationFailed };

Action action_of(Update update);
Action action_of(FinalResult result);

// `ClientCommonPacketListenerImpl.parseResourcePackUrl`: only http and https,
// and a url that does not parse is answered with INVALID_URL rather than
// being downloaded.
bool pack_url_allowed(std::string_view url);

// `DownloadedPackSource.tryParseSha1Hash`: forty hex digits, lowercased; a
// hash that is not that is treated as no hash at all, which means the file is
// fetched and kept under whatever hash it turns out to have.
std::optional<std::string> parse_sha1(std::string_view hash);

// `ServerPackManager`: the whole decision. It owns no network and no
// textures; a download and a reload are handed in, so the order of the
// states can be tested on its own.
class ServerPackManager {
public:
    // `DownloadQueue.DownloadRequest`.
    struct DownloadRequest {
        PackId id{};
        std::string url;
        std::optional<std::string> hash;
    };
    // `DownloadQueue.BatchResult`.
    struct BatchResult {
        std::vector<std::pair<PackId, std::filesystem::path>> downloaded;
        std::vector<PackId> failed;
    };
    // `PackReloadConfig.IdAndPath`, bottom pack first.
    struct IdAndPath {
        PackId id{};
        std::filesystem::path path;
    };

    struct Hooks {
        // `PackDownloader.download`: fetch this batch and call back with what
        // arrived. The callback may be made from another thread; nothing here
        // is touched until deliver_download runs on the ticking thread.
        std::function<void(std::vector<DownloadRequest>)> download;
        // `PackReloadConfig.scheduleReload`: load these packs, in this order,
        // and report back through reload_succeeded or reload_failed.
        std::function<void(std::vector<IdAndPath>)> schedule_reload;
        // `PackLoadFeedback`: what the server is told.
        std::function<void(const PackId&, Action)> respond;
    };

    ServerPackManager(Hooks hooks, PromptStatus status);

    // `ServerPackManager.pushPack`.
    void push_pack(const PackId& id, std::string url, std::optional<std::string> hash);
    // `ServerPackManager.pushLocalPack`: a pack already on disk, which skips
    // straight past the download.
    void push_local_pack(const PackId& id, std::filesystem::path path);
    // `ServerPackManager.popPack` and `.popAll`.
    void pop_pack(const PackId& id);
    void pop_all();

    // `ServerPackManager.allowServerPacks` and `.rejectServerPacks`: the two
    // answers to the prompt.
    void allow_server_packs();
    void reject_server_packs();
    // `ServerPackManager.resetPromptStatus`.
    void reset_prompt_status();

    // `ServerPackManager.tick`: downloads first, then the reload, then the
    // packs whose answer has been sent are forgotten.
    void tick();
    // `DownloadedPackSource.createUpdateScheduler`: a change asks for a tick,
    // and a tick may ask for another. This runs them until they settle.
    void run_updates();

    // `ServerPackManager.onDownload`, once the batch is back on this thread.
    void deliver_download(const BatchResult& result);
    // `PackReloadConfig.Callbacks.onSuccess` / `.onFailure`.
    void reload_succeeded();
    void reload_failed(bool give_up);

    // `DownloadedPackSource.cleanupAfterDisconnect`.
    void cleanup_after_disconnect();

    PromptStatus prompt_status() const { return prompt_status_; }
    // `PackReloadConfig.Callbacks.packsToLoad`: the stack the reload in flight
    // is to show, the pack the server pushed first at the bottom.
    std::vector<IdAndPath> packs_to_load() const;
    // The packs on screen now, in the same order.
    std::vector<IdAndPath> applied() const;
    size_t tracked() const { return packs_.size(); }
    bool reload_pending() const { return reload_pending_; }

private:
    // `ServerPackManager.PackDownloadStatus` and `.ActivationStatus`.
    enum class DownloadStatus { Requested, Pending, Done };
    enum class ActivationStatus { Inactive, Pending, Active };
    // `ServerPackManager.RemovalReason`: why a pack is going, and what the
    // server is told about it, if anything.
    enum class RemovalReason {
        DownloadFailed, ActivationFailed, Declined, Discarded, ServerRemoved, ServerReplaced
    };
    static std::optional<FinalResult> server_response(RemovalReason reason);

    // `ServerPackManager.ServerPackData`.
    struct PackData {
        PackId id{};
        std::string url;
        std::optional<std::string> hash;
        std::filesystem::path path;
        std::optional<RemovalReason> removal_reason;
        DownloadStatus download_status = DownloadStatus::Requested;
        ActivationStatus activation_status = ActivationStatus::Inactive;
        bool prompt_accepted = false;

        bool removed() const { return removal_reason.has_value(); }
        void set_removal_reason_if_not_set(RemovalReason reason) {
            if (!removal_reason.has_value()) removal_reason = reason;
        }
    };

    void mark_existing_packs_as_removed(const PackId& id);
    void push_new_pack(PackData pack);
    void accept_pack(PackData& pack);
    PackData* find_pack_info(const PackId& id);
    void register_for_update();
    void cleanup_removed_packs();
    bool update_downloads();
    void trigger_reload_if_needed();
    void report_update(const PackId& id, Update update);
    void report_final_result(const PackId& id, FinalResult result);

    Hooks hooks_;
    PromptStatus prompt_status_ = PromptStatus::Pending;
    std::vector<PackData> packs_;
    bool has_updates_ = false;
    bool running_updates_ = false;
    bool reload_pending_ = false;
    std::vector<PackId> reload_to_activate_;
    std::vector<PackId> reload_to_deactivate_;
};

} // namespace mcpp::packs
