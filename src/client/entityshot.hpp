#pragma once
// The entity area's subcommands: models drawn with no server, a second player
// driven by script, and recordings of it replayed into pictures.

namespace client {

// Returns -1 when the command is not one of the entity area's.
int entity_command(int argc, char** argv);

} // namespace client
