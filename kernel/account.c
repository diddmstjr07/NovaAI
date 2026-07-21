#include "account.h"
#include "filesystem.h"
#include "runtime.h"

#define ACCOUNT_MAGIC "NOVAUSR1"
#define ACCOUNT_MAX 8
#define ACCOUNT_ADMIN 1

typedef struct __attribute__((packed)) {
    char name[24];
    uint32_t uid;
    uint32_t flags;
    uint64_t salt;
    uint64_t password_hash;
} AccountRecord;

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t version;
    uint32_t count;
    AccountRecord records[ACCOUNT_MAX];
} AccountDatabase;

static AccountDatabase database;
static int current_index = -1;

static uint64_t password_hash(const char *password, uint64_t salt) {
    uint64_t hash = 1469598103934665603UL ^ salt;
    while (*password) {
        hash ^= (uint8_t)*password++;
        hash *= 1099511628211UL;
    }
    hash ^= salt >> 32;
    hash *= 1099511628211UL;
    return hash;
}

static bool valid_account_name(const char *name) {
    size_t length = strlen(name);
    if (!length || length > ACCOUNT_NAME_MAX) return false;
    for (size_t index = 0; index < length; ++index) {
        char character = name[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

static bool save_database(void) {
    bool saved = fs_write("users.db", &database, sizeof(database));
    if (saved) saved = fs_chmod("users.db", 0600);
    return saved;
}

static void add_default(int index, const char *name, uint32_t uid,
                        const char *password, bool administrator) {
    AccountRecord *record = &database.records[index];
    strncpy(record->name, name, sizeof(record->name) - 1);
    record->uid = uid;
    record->flags = administrator ? ACCOUNT_ADMIN : 0;
    record->salt = 0x4E4F564100000000UL ^ ((uint64_t)uid << 17) ^ strlen(name);
    record->password_hash = password_hash(password, record->salt);
}

bool account_init(void) {
    fs_set_identity(0, true);
    int loaded = fs_read("users.db", &database, sizeof(database));
    if (loaded != (int)sizeof(database) || memcmp(database.magic, ACCOUNT_MAGIC, 8) ||
        database.version != 1 || database.count < 2 || database.count > ACCOUNT_MAX) {
        memset(&database, 0, sizeof(database));
        memcpy(database.magic, ACCOUNT_MAGIC, 8);
        database.version = 1;
        database.count = 2;
        add_default(0, "root", 0, "nova", true);
        add_default(1, "eunseokyang", 1000, "", false);
        if (!save_database()) return false;
    }
    return account_login("eunseokyang", "");
}

bool account_login(const char *name, const char *password) {
    if (!name || !password) return false;
    for (uint32_t index = 0; index < database.count; ++index) {
        AccountRecord *record = &database.records[index];
        if (!strcmp(record->name, name) &&
            record->password_hash == password_hash(password, record->salt)) {
            current_index = (int)index;
            fs_set_identity(record->uid, (record->flags & ACCOUNT_ADMIN) != 0);
            return true;
        }
    }
    return false;
}

bool account_create(const char *name, const char *password, bool administrator) {
    if (current_index < 0 || !account_current_is_admin() || !valid_account_name(name) ||
        !password || database.count >= ACCOUNT_MAX) return false;
    for (uint32_t index = 0; index < database.count; ++index) {
        if (!strcmp(database.records[index].name, name)) return false;
    }
    uint32_t index = database.count;
    uint32_t uid = 1000 + index;
    memset(&database.records[index], 0, sizeof(AccountRecord));
    add_default((int)index, name, uid, password, administrator);
    database.count++;
    if (!save_database()) {
        database.count--;
        return false;
    }
    return true;
}

const char *account_current_name(void) {
    return current_index >= 0 ? database.records[current_index].name : "none";
}

uint32_t account_current_uid(void) {
    return current_index >= 0 ? database.records[current_index].uid : 0;
}

bool account_current_is_admin(void) {
    return current_index >= 0 && (database.records[current_index].flags & ACCOUNT_ADMIN) != 0;
}

int account_count(void) {
    return (int)database.count;
}

bool account_info(int index, AccountInfo *info) {
    if (!info || index < 0 || index >= (int)database.count) return false;
    AccountRecord *record = &database.records[index];
    strcpy(info->name, record->name);
    info->uid = record->uid;
    info->administrator = (record->flags & ACCOUNT_ADMIN) != 0;
    return true;
}
