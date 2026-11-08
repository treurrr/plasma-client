#include "modrinth_store.hpp"

#include <dlfcn.h>

namespace mcpp::auth {
namespace {

// sqlite's own header is part of its development package, which a player's
// machine has no reason to carry; the library itself is on every desktop. So the
// handful of calls needed to read one table are declared here and looked up at
// run time, and Modrinth is simply skipped when the library is absent.
constexpr int kOpenReadOnly = 0x00000001;
constexpr int kOpenUri = 0x00000040;
constexpr int kRow = 100;

struct Sqlite {
    void* library = nullptr;
    int (*open)(const char*, void**, int, const char*) = nullptr;
    int (*prepare)(void*, const char*, int, void**, const char**) = nullptr;
    int (*step)(void*) = nullptr;
    const unsigned char* (*text)(void*, int) = nullptr;
    long long (*int64)(void*, int) = nullptr;
    int (*finalize)(void*) = nullptr;
    int (*close)(void*) = nullptr;

    bool ok() const { return library != nullptr && open != nullptr && prepare != nullptr; }
};

template <typename Fn>
void bind(void* library, const char* name, Fn& into) {
    into = reinterpret_cast<Fn>(dlsym(library, name));
}

const Sqlite& sqlite() {
    static const Sqlite loaded = [] {
        Sqlite out;
        for (const char* name : {"libsqlite3.so.0", "libsqlite3.so", "libsqlite3.0.dylib",
                                 "libsqlite3.dylib"}) {
            out.library = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
            if (out.library != nullptr) break;
        }
        if (out.library == nullptr) return out;
        bind(out.library, "sqlite3_open_v2", out.open);
        bind(out.library, "sqlite3_prepare_v2", out.prepare);
        bind(out.library, "sqlite3_step", out.step);
        bind(out.library, "sqlite3_column_text", out.text);
        bind(out.library, "sqlite3_column_int64", out.int64);
        bind(out.library, "sqlite3_finalize", out.finalize);
        bind(out.library, "sqlite3_close", out.close);
        return out;
    }();
    return loaded;
}

std::string column(const Sqlite& api, void* statement, int index) {
    const unsigned char* value = api.text(statement, index);
    return value == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(value));
}

} // namespace

std::vector<ModrinthAccount> read_modrinth_accounts(const std::filesystem::path& database,
                                                    bool* sqlite_missing) {
    std::vector<ModrinthAccount> accounts;
    const Sqlite& api = sqlite();
    if (sqlite_missing != nullptr) *sqlite_missing = !api.ok();
    if (!api.ok()) return accounts;

    // Opened read only and through a uri, so a Modrinth that is running keeps
    // its file to itself and nothing here can write to it.
    const std::string uri = "file:" + database.string() + "?mode=ro";
    void* handle = nullptr;
    if (api.open(uri.c_str(), &handle, kOpenReadOnly | kOpenUri, nullptr) != 0 ||
        handle == nullptr) {
        if (handle != nullptr && api.close != nullptr) api.close(handle);
        return accounts;
    }

    void* statement = nullptr;
    const char* query =
        "select uuid, username, access_token, expires, active from minecraft_users";
    if (api.prepare(handle, query, -1, &statement, nullptr) == 0 && statement != nullptr) {
        while (api.step(statement) == kRow) {
            ModrinthAccount account;
            account.uuid = column(api, statement, 0);
            account.username = column(api, statement, 1);
            account.token = column(api, statement, 2);
            account.expires = api.int64(statement, 3);
            account.active = api.int64(statement, 4) != 0;
            if (account.uuid.empty() || account.username.empty()) continue;
            accounts.push_back(std::move(account));
        }
        api.finalize(statement);
    }
    api.close(handle);
    return accounts;
}

} // namespace mcpp::auth
