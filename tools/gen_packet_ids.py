#!/usr/bin/env python3
"""Emit src/proto/packet_ids_gen.cpp from Mojang's own packets.json reports.

Usage: gen_packet_ids.py <protocol>=<reports/packets.json> ...

Generate a report with the official server jar, which is observable output and
not decompiled code:
    java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports
"""
import json
import sys
from pathlib import Path

# field name -> packet name in the report. A packet the version does not have
# becomes -1, which is how 1.8.9 will carry an empty configuration state.
CURATED = {
    "login_cb": ("login", "clientbound", {
        "disconnect": "login_disconnect", "hello": "hello", "finished": "login_finished",
        "compression": "login_compression", "custom_query": "custom_query",
        "cookie_request": "cookie_request",
    }),
    "login_sb": ("login", "serverbound", {
        "hello": "hello", "key": "key", "custom_query_answer": "custom_query_answer",
        "acknowledged": "login_acknowledged", "cookie_response": "cookie_response",
    }),
    "config_cb": ("configuration", "clientbound", {
        "cookie_request": "cookie_request", "custom_payload": "custom_payload",
        "disconnect": "disconnect", "finish": "finish_configuration", "keep_alive": "keep_alive",
        "ping": "ping", "registry_data": "registry_data", "reset_chat": "reset_chat",
        "resource_pack_push": "resource_pack_push", "select_known_packs": "select_known_packs",
        "store_cookie": "store_cookie", "transfer": "transfer", "update_tags": "update_tags",
        "update_enabled_features": "update_enabled_features", "code_of_conduct": "code_of_conduct",
        "show_dialog": "show_dialog",
    }),
    "config_sb": ("configuration", "serverbound", {
        "client_information": "client_information", "cookie_response": "cookie_response",
        "custom_payload": "custom_payload", "finish": "finish_configuration",
        "keep_alive": "keep_alive", "pong": "pong", "resource_pack": "resource_pack",
        "select_known_packs": "select_known_packs",
        "accept_code_of_conduct": "accept_code_of_conduct",
    }),
    "play_cb": ("play", "clientbound", {
        "login": "login", "keep_alive": "keep_alive", "ping": "ping", "disconnect": "disconnect",
        "system_chat": "system_chat", "player_chat": "player_chat",
        "disguised_chat": "disguised_chat", "start_configuration": "start_configuration",
        "player_position": "player_position", "custom_payload": "custom_payload",
        "set_health": "set_health", "respawn": "respawn", "game_event": "game_event",
    }),
    "play_sb": ("play", "serverbound", {
        "keep_alive": "keep_alive", "pong": "pong", "chat": "chat",
        "chat_command": "chat_command", "chat_ack": "chat_ack",
        "client_information": "client_information",
        "configuration_acknowledged": "configuration_acknowledged",
        "custom_payload": "custom_payload", "accept_teleportation": "accept_teleportation",
        "move_player_pos": "move_player_pos", "move_player_pos_rot": "move_player_pos_rot",
        "client_tick_end": "client_tick_end",
    }),
}


def resolve(report, state, direction, packet):
    entry = report.get(state, {}).get(direction, {}).get("minecraft:" + packet)
    return entry["protocol_id"] if entry else -1


def main(args):
    if not args:
        sys.exit(__doc__)

    versions = []
    for arg in args:
        protocol, _, path = arg.partition("=")
        report = json.loads(Path(path).read_text())
        groups = {}
        for group, (state, direction, fields) in CURATED.items():
            groups[group] = {name: resolve(report, state, direction, packet)
                             for name, packet in fields.items()}
            for name, value in groups[group].items():
                if value < 0:
                    print(f"  protocol {protocol}: {group}.{name} bestaat niet", file=sys.stderr)
        versions.append((int(protocol), groups))
    versions.sort()

    out = ['// Gegenereerd door tools/gen_packet_ids.py. Niet met de hand aanpassen.',
           '#include "mcpp/proto/packet_ids.hpp"', '', 'namespace mcpp::proto {', '',
           'namespace {', '', 'constexpr PacketIds kTables[] = {']
    for protocol, groups in versions:
        out.append(f"    {{")
        out.append(f"        .protocol = {protocol},")
        for group, (_, _, fields) in CURATED.items():
            inner = ", ".join(f".{name} = {groups[group][name]}" for name in fields)
            out.append(f"        .{group} = {{{inner}}},")
        out.append("    },")
    out += ['};', '', '} // namespace', '',
            'const PacketIds* ids_for(int32_t protocol) {',
            '    for (const auto& table : kTables) {',
            '        if (table.protocol == protocol) return &table;',
            '    }',
            '    return nullptr;',
            '}', '',
            'std::span<const PacketIds> all_tables() { return {kTables, std::size(kTables)}; }',
            '', '} // namespace mcpp::proto', '']
    print("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1:])
