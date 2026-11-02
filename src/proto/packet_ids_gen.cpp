// Gegenereerd door tools/gen_packet_ids.py. Niet met de hand aanpassen.
#include "mcpp/proto/packet_ids.hpp"

namespace mcpp::proto {

namespace {

constexpr PacketIds kTables[] = {
    {
        .protocol = 774,
        .login_cb = {.disconnect = 0, .hello = 1, .finished = 2, .compression = 3, .custom_query = 4, .cookie_request = 5},
        .login_sb = {.hello = 0, .key = 1, .custom_query_answer = 2, .acknowledged = 3, .cookie_response = 4},
        .config_cb = {.cookie_request = 0, .custom_payload = 1, .disconnect = 2, .finish = 3, .keep_alive = 4, .ping = 5, .registry_data = 7, .reset_chat = 6, .resource_pack_push = 9, .select_known_packs = 14, .store_cookie = 10, .transfer = 11, .update_tags = 13, .update_enabled_features = 12, .code_of_conduct = 19, .show_dialog = 18},
        .config_sb = {.client_information = 0, .cookie_response = 1, .custom_payload = 2, .finish = 3, .keep_alive = 4, .pong = 5, .resource_pack = 6, .select_known_packs = 7, .accept_code_of_conduct = 9},
        .play_cb = {.login = 48, .keep_alive = 43, .ping = 59, .disconnect = 32, .system_chat = 119, .player_chat = 63, .disguised_chat = 33, .start_configuration = 116, .player_position = 70, .custom_payload = 24, .set_health = 102, .respawn = 80, .game_event = 38, .level_chunk_with_light = 44, .chunk_batch_start = 12, .chunk_batch_finished = 11, .forget_level_chunk = 37, .light_update = 47, .block_update = 8, .section_blocks_update = 82, .set_chunk_cache_center = 92, .set_chunk_cache_radius = 93},
        .play_sb = {.keep_alive = 27, .pong = 44, .chat = 8, .chat_command = 6, .chat_ack = 5, .client_information = 13, .configuration_acknowledged = 15, .custom_payload = 21, .accept_teleportation = 0, .move_player_pos = 29, .move_player_pos_rot = 30, .client_tick_end = 12, .chunk_batch_received = 10},
    },
    {
        .protocol = 776,
        .login_cb = {.disconnect = 0, .hello = 1, .finished = 2, .compression = 3, .custom_query = 4, .cookie_request = 5},
        .login_sb = {.hello = 0, .key = 1, .custom_query_answer = 2, .acknowledged = 3, .cookie_response = 4},
        .config_cb = {.cookie_request = 0, .custom_payload = 1, .disconnect = 2, .finish = 3, .keep_alive = 4, .ping = 5, .registry_data = 7, .reset_chat = 6, .resource_pack_push = 9, .select_known_packs = 14, .store_cookie = 10, .transfer = 11, .update_tags = 13, .update_enabled_features = 12, .code_of_conduct = 19, .show_dialog = 18},
        .config_sb = {.client_information = 0, .cookie_response = 1, .custom_payload = 2, .finish = 3, .keep_alive = 4, .pong = 5, .resource_pack = 6, .select_known_packs = 7, .accept_code_of_conduct = 9},
        .play_cb = {.login = 49, .keep_alive = 44, .ping = 61, .disconnect = 32, .system_chat = 121, .player_chat = 65, .disguised_chat = 33, .start_configuration = 118, .player_position = 72, .custom_payload = 24, .set_health = 104, .respawn = 82, .game_event = 38, .level_chunk_with_light = 45, .chunk_batch_start = 12, .chunk_batch_finished = 11, .forget_level_chunk = 37, .light_update = 48, .block_update = 8, .section_blocks_update = 84, .set_chunk_cache_center = 94, .set_chunk_cache_radius = 95},
        .play_sb = {.keep_alive = 28, .pong = 45, .chat = 9, .chat_command = 7, .chat_ack = 6, .client_information = 14, .configuration_acknowledged = 16, .custom_payload = 22, .accept_teleportation = 0, .move_player_pos = 30, .move_player_pos_rot = 31, .client_tick_end = 13, .chunk_batch_received = 11},
    },
};

} // namespace

const PacketIds* ids_for(int32_t protocol) {
    for (const auto& table : kTables) {
        if (table.protocol == protocol) return &table;
    }
    return nullptr;
}

std::span<const PacketIds> all_tables() { return {kTables, std::size(kTables)}; }

} // namespace mcpp::proto

