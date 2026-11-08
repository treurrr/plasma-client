#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mcpp::auth {

// One row of the Modrinth app's `minecraft_users` table.
struct ModrinthAccount {
    std::string uuid;
    std::string username;
    std::string token;
    long long expires = 0;  // seconds since the epoch
    bool active = false;
};

// The accounts the Modrinth app is holding. `sqlite_missing` is set when the
// system has no sqlite library to read the file with, which is the difference
// between "no accounts" and "could not look".
std::vector<ModrinthAccount> read_modrinth_accounts(const std::filesystem::path& database,
                                                    bool* sqlite_missing = nullptr);

} // namespace mcpp::auth
