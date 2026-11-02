#pragma once
#include <cstdint>
#include <span>

// Login and configuration ids happen to match between 774 and 776, but 102 of
// the 139 clientbound play ids differ. Every id therefore comes from the
// version's own table; nothing is shared by assumption.
namespace mcpp::proto {

struct PacketIds {
    int32_t protocol;

    struct LoginClientbound {
        int32_t disconnect, hello, finished, compression, custom_query, cookie_request;
    } login_cb;

    struct LoginServerbound {
        int32_t hello, key, custom_query_answer, acknowledged, cookie_response;
    } login_sb;

    struct ConfigClientbound {
        int32_t cookie_request, custom_payload, disconnect, finish, keep_alive, ping,
            registry_data, reset_chat, resource_pack_push, select_known_packs, store_cookie,
            transfer, update_tags, update_enabled_features, code_of_conduct, show_dialog;
    } config_cb;

    struct ConfigServerbound {
        int32_t client_information, cookie_response, custom_payload, finish, keep_alive, pong,
            resource_pack, select_known_packs, accept_code_of_conduct;
    } config_sb;

    struct PlayClientbound {
        int32_t login, keep_alive, ping, disconnect, system_chat, player_chat, disguised_chat,
            start_configuration, player_position, custom_payload, set_health, respawn, game_event;
    } play_cb;

    struct PlayServerbound {
        int32_t keep_alive, pong, chat, chat_command, chat_ack, client_information,
            configuration_acknowledged, custom_payload, accept_teleportation, move_player_pos,
            move_player_pos_rot, client_tick_end;
    } play_sb;
};

const PacketIds* ids_for(int32_t protocol);
std::span<const PacketIds> all_tables();

} // namespace mcpp::proto
