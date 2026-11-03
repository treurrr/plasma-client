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
            start_configuration, player_position, custom_payload, set_health, respawn, game_event,
            level_chunk_with_light, chunk_batch_start, chunk_batch_finished, forget_level_chunk,
            light_update, block_update, section_blocks_update, set_chunk_cache_center,
            set_chunk_cache_radius, add_entity, remove_entities, move_entity_pos,
            move_entity_pos_rot, move_entity_rot, entity_position_sync, player_info_update,
            player_info_remove, container_set_content, container_set_slot, container_close,
            set_held_slot, set_experience, set_cursor_item, set_player_inventory,
            block_changed_ack, bundle_delimiter, player_abilities, change_difficulty,
            update_mob_effect, remove_mob_effect, update_tags, update_attributes;
    } play_cb;

    struct PlayServerbound {
        int32_t keep_alive, pong, chat, chat_command, chat_ack, client_information,
            configuration_acknowledged, custom_payload, accept_teleportation, move_player_pos,
            move_player_pos_rot, client_tick_end, chunk_batch_received, set_carried_item,
            container_click, container_close, player_action, use_item_on, use_item, swing,
            player_loaded, client_command, player_abilities;
    } play_sb;
};

const PacketIds* ids_for(int32_t protocol);
std::span<const PacketIds> all_tables();

} // namespace mcpp::proto
