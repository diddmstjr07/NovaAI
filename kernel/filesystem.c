#include "filesystem.h"
#include "ata.h"
#include "heap.h"
#include "runtime.h"

#define FS_VERSION 2
#define FS_LEGACY_VERSION 1
#define FS_SUPER_LBA 4096
#define FS_DIRECTORY_LBA (FS_SUPER_LBA + 1)
#define FS_DIRECTORY_SECTORS 256
#define FS_DATA_LBA 4608
#define FS_MAX_ENTRIES 1024
#define FS_LEGACY_DIRECTORY_SECTORS 8
#define FS_LEGACY_DATA_LBA 4128
#define FS_LEGACY_MAX_ENTRIES 64

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t version;
    uint32_t directory_lba;
    uint32_t directory_sectors;
    uint32_t data_lba;
    uint32_t max_entries;
    uint32_t total_sectors;
    uint8_t reserved[480];
} FsSuperblock;

typedef struct __attribute__((packed)) {
    uint8_t used;
    uint8_t type;
    uint16_t flags;
    uint32_t owner;
    char name[FS_NAME_MAX + 1];
    uint32_t start_lba;
    uint64_t size;
    uint32_t capacity_sectors;
    uint32_t modified;
    uint32_t reserved;
} FsEntry;

typedef struct __attribute__((packed)) {
    uint8_t used;
    uint8_t type;
    uint16_t flags;
    char name[40];
    uint32_t start_lba;
    uint32_t size;
    uint32_t capacity_sectors;
    uint32_t modified;
    uint32_t owner;
} LegacyFsEntry;

typedef struct {
    LegacyFsEntry entry;
    uint8_t *data;
} LegacyBackup;

typedef char superblock_size_must_be_512[(sizeof(FsSuperblock) == 512) ? 1 : -1];
typedef char entry_size_must_be_128[(sizeof(FsEntry) == 128) ? 1 : -1];
typedef char legacy_entry_size_must_be_64[(sizeof(LegacyFsEntry) == 64) ? 1 : -1];

static FsSuperblock superblock;
static FsEntry *entries;
static bool mounted;
static uint32_t current_uid;
static bool current_privileged = true;

static int find_entry(const char *name);

static uint16_t entry_mode(const FsEntry *entry) {
    uint16_t mode = entry->flags & 0777;
    if (mode) return mode;
    return entry->type == FS_TYPE_DIRECTORY || entry->type == FS_TYPE_SYMLINK ? 0777 : 0666;
}

static bool may_read(const FsEntry *entry) {
    if (current_privileged) return true;
    uint16_t mode = entry_mode(entry);
    return current_uid == entry->owner ? (mode & 0400) != 0 : (mode & 0004) != 0;
}

static bool may_write(const FsEntry *entry) {
    if (current_privileged) return true;
    uint16_t mode = entry_mode(entry);
    return current_uid == entry->owner ? (mode & 0200) != 0 : (mode & 0002) != 0;
}

static bool valid_name(const char *name) {
    size_t length = strlen(name);
    if (!length || length > FS_NAME_MAX) return false;
    size_t segment = 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)name[index];
        if (character < 32 || character > 126) return false;
        if (character == '/') {
            if (index == segment ||
                (index - segment == 2 && name[segment] == '.' && name[segment + 1] == '.')) {
                return false;
            }
            segment = index + 1;
        }
    }
    return segment < length &&
           !(length - segment == 2 && name[segment] == '.' && name[segment + 1] == '.');
}

static bool valid_symlink_target(const char *target) {
    if (*target == '/') ++target;
    return valid_name(target);
}

static bool magic_matches(const FsSuperblock *block) {
    static const char magic[8] = {'N','O','V','A','6','4','F','S'};
    return !memcmp(block->magic, magic, sizeof(magic));
}

static int find_entry(const char *name) {
    for (int index = 0; index < FS_MAX_ENTRIES; ++index) {
        if (entries[index].used && !strcmp(entries[index].name, name)) return index;
    }
    return -1;
}

static bool parent_directory_exists(const char *name) {
    const char *separator = NULL;
    for (const char *cursor = name; *cursor; ++cursor) {
        if (*cursor == '/') separator = cursor;
    }
    if (!separator) return true;
    size_t length = (size_t)(separator - name);
    if (!length || length > FS_NAME_MAX) return false;
    char parent[FS_NAME_MAX + 1];
    memcpy(parent, name, length);
    parent[length] = 0;
    int index = find_entry(parent);
    return index >= 0 && entries[index].type == FS_TYPE_DIRECTORY;
}

static int allocate_entry(const char *name, uint8_t type, uint16_t mode) {
    if (!valid_name(name) || find_entry(name) >= 0 || !parent_directory_exists(name)) return -1;
    for (int index = 0; index < FS_MAX_ENTRIES; ++index) {
        if (entries[index].used) continue;
        memset(&entries[index], 0, sizeof(entries[index]));
        entries[index].used = 1;
        entries[index].type = type;
        entries[index].flags = mode & 0777;
        entries[index].owner = current_uid;
        strncpy(entries[index].name, name, sizeof(entries[index].name) - 1);
        return index;
    }
    return -1;
}

static bool save_entry(int index) {
    uint8_t *sector = heap_alloc(ATA_SECTOR_SIZE);
    if (!sector) return false;
    int per_sector = ATA_SECTOR_SIZE / (int)sizeof(FsEntry);
    uint32_t sector_lba = FS_DIRECTORY_LBA + (uint32_t)index / (uint32_t)per_sector;
    bool success = ata_read_sector(sector_lba, sector);
    if (success) {
        memcpy(sector + ((index % per_sector) * sizeof(FsEntry)),
               &entries[index], sizeof(FsEntry));
        success = ata_write_sector(sector_lba, sector);
    }
    heap_free(sector);
    return success;
}

static bool format_v2(void) {
    if (!entries || ata_sector_count() <= FS_DATA_LBA + 128) return false;
    memset(&superblock, 0, sizeof(superblock));
    memcpy(superblock.magic, "NOVA64FS", 8);
    superblock.version = FS_VERSION;
    superblock.directory_lba = FS_DIRECTORY_LBA;
    superblock.directory_sectors = FS_DIRECTORY_SECTORS;
    superblock.data_lba = FS_DATA_LBA;
    superblock.max_entries = FS_MAX_ENTRIES;
    superblock.total_sectors = ata_sector_count();
    memset(entries, 0, sizeof(FsEntry) * FS_MAX_ENTRIES);
    if (!ata_write_sector(FS_SUPER_LBA, &superblock)) return false;
    uint8_t *empty = heap_calloc(1, ATA_SECTOR_SIZE);
    if (!empty) return false;
    bool success = true;
    for (uint32_t sector = 0; sector < FS_DIRECTORY_SECTORS; ++sector) {
        if (!ata_write_sector(FS_DIRECTORY_LBA + sector, empty)) {
            success = false;
            break;
        }
    }
    heap_free(empty);
    mounted = success;
    return success;
}

static int read_raw_data(uint32_t start_lba, uint64_t size, uint64_t offset,
                         void *buffer, size_t capacity) {
    if (offset >= size) return 0;
    uint64_t available = size - offset;
    if ((uint64_t)capacity > available) capacity = (size_t)available;
    uint8_t *sector = heap_alloc(ATA_SECTOR_SIZE);
    if (!sector) return -1;
    size_t copied = 0;
    while (copied < capacity) {
        uint64_t position = offset + copied;
        uint32_t lba = start_lba + (uint32_t)(position / ATA_SECTOR_SIZE);
        if (!ata_read_sector(lba, sector)) {
            heap_free(sector);
            return -1;
        }
        size_t sector_offset = (size_t)(position % ATA_SECTOR_SIZE);
        size_t chunk = capacity - copied;
        if (chunk > ATA_SECTOR_SIZE - sector_offset) chunk = ATA_SECTOR_SIZE - sector_offset;
        memcpy((uint8_t *)buffer + copied, sector + sector_offset, chunk);
        copied += chunk;
    }
    heap_free(sector);
    return copied > 0x7FFFFFFFUL ? -1 : (int)copied;
}

static int read_entry_data(const FsEntry *entry, uint64_t offset,
                           void *buffer, size_t capacity) {
    return read_raw_data(entry->start_lba, entry->size, offset, buffer, capacity);
}

static int resolve_entry(const char *name) {
    char resolved[FS_NAME_MAX + 1];
    strcpy(resolved, name);
    for (int depth = 0; depth < 8; ++depth) {
        int index = find_entry(resolved);
        if (index < 0) return -1;
        if (entries[index].type != FS_TYPE_SYMLINK) return index;
        char target[FS_NAME_MAX + 2];
        if (!entries[index].size || entries[index].size > FS_NAME_MAX + 1 ||
            read_entry_data(&entries[index], 0, target, (size_t)entries[index].size) !=
                (int)entries[index].size) return -1;
        target[entries[index].size] = 0;
        if (!valid_symlink_target(target)) return -1;
        if (target[0] == '/') {
            strcpy(resolved, target + 1);
        } else {
            size_t directory_length = 0;
            for (size_t cursor = 0; resolved[cursor]; ++cursor) {
                if (resolved[cursor] == '/') directory_length = cursor;
            }
            size_t target_length = strlen(target);
            if (directory_length) {
                if (directory_length + 1 + target_length > FS_NAME_MAX) return -1;
                resolved[directory_length] = '/';
                strcpy(resolved + directory_length + 1, target);
            } else {
                strcpy(resolved, target);
            }
        }
        if (!valid_name(resolved)) return -1;
    }
    return -1;
}

static uint32_t next_data_lba(void) {
    uint32_t next = FS_DATA_LBA;
    for (int index = 0; index < FS_MAX_ENTRIES; ++index) {
        if (!entries[index].used) continue;
        uint64_t end = (uint64_t)entries[index].start_lba + entries[index].capacity_sectors;
        if (end > next && end <= 0xFFFFFFFFUL) next = (uint32_t)end;
    }
    return next;
}

static bool write_entry_data(FsEntry *entry, int index, const void *data, size_t size) {
    uint64_t required64 = ((uint64_t)size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (!required64) required64 = 1;
    if (required64 > 0xFFFFFFFFUL) return false;
    uint32_t required = (uint32_t)required64;
    if (entry->capacity_sectors < required) {
        entry->start_lba = next_data_lba();
        entry->capacity_sectors = required;
    }
    if ((uint64_t)entry->start_lba + required > ata_sector_count()) return false;
    uint8_t *sector = heap_alloc(ATA_SECTOR_SIZE);
    if (!sector) return false;
    size_t written = 0;
    bool success = true;
    for (uint32_t block = 0; block < required; ++block) {
        memset(sector, 0, ATA_SECTOR_SIZE);
        size_t chunk = size - written;
        if (chunk > ATA_SECTOR_SIZE) chunk = ATA_SECTOR_SIZE;
        if (chunk) memcpy(sector, (const uint8_t *)data + written, chunk);
        if (!ata_write_sector(entry->start_lba + block, sector)) {
            success = false;
            break;
        }
        written += chunk;
    }
    heap_free(sector);
    if (!success) return false;
    entry->size = size;
    entry->modified++;
    return save_entry(index);
}

static int path_depth(const char *path) {
    int depth = 0;
    for (; *path; ++path) if (*path == '/') ++depth;
    return depth;
}

static void release_legacy_backup(LegacyBackup *backup) {
    if (!backup) return;
    for (int index = 0; index < FS_LEGACY_MAX_ENTRIES; ++index) {
        if (backup[index].data) heap_free(backup[index].data);
    }
    heap_free(backup);
}

static bool migrate_legacy(void) {
    LegacyBackup *backup = heap_calloc(FS_LEGACY_MAX_ENTRIES, sizeof(*backup));
    LegacyFsEntry *legacy = heap_alloc(sizeof(LegacyFsEntry) * FS_LEGACY_MAX_ENTRIES);
    if (!backup || !legacy) {
        heap_free(backup);
        heap_free(legacy);
        return false;
    }
    bool loaded = true;
    for (uint32_t sector = 0; sector < FS_LEGACY_DIRECTORY_SECTORS; ++sector) {
        if (!ata_read_sector(FS_DIRECTORY_LBA + sector,
                             (uint8_t *)legacy + sector * ATA_SECTOR_SIZE)) {
            loaded = false;
            break;
        }
    }
    for (int index = 0; loaded && index < FS_LEGACY_MAX_ENTRIES; ++index) {
        if (!legacy[index].used) continue;
        backup[index].entry = legacy[index];
        if (!legacy[index].size) continue;
        backup[index].data = heap_alloc(legacy[index].size);
        if (!backup[index].data ||
            read_raw_data(legacy[index].start_lba, legacy[index].size, 0,
                          backup[index].data, legacy[index].size) != (int)legacy[index].size) {
            loaded = false;
        }
    }
    heap_free(legacy);
    if (!loaded || !format_v2()) {
        release_legacy_backup(backup);
        return false;
    }

    bool success = true;
    for (int depth = 0; success && depth < FS_NAME_MAX; ++depth) {
        for (int index = 0; index < FS_LEGACY_MAX_ENTRIES; ++index) {
            LegacyFsEntry *old = &backup[index].entry;
            if (!old->used || old->type != FS_TYPE_DIRECTORY || path_depth(old->name) != depth) {
                continue;
            }
            FsFileInfo existing;
            if (!fs_path_info(old->name, &existing, false) && !fs_mkdir(old->name, old->flags)) {
                success = false;
                break;
            }
            fs_chmod(old->name, old->flags & 0777);
            fs_chown(old->name, old->owner);
        }
    }
    for (int index = 0; success && index < FS_LEGACY_MAX_ENTRIES; ++index) {
        LegacyFsEntry *old = &backup[index].entry;
        if (!old->used || old->type == FS_TYPE_DIRECTORY) continue;
        if (old->type == FS_TYPE_SYMLINK) {
            if (!old->size || old->size > FS_NAME_MAX) {
                success = false;
                break;
            }
            char target[FS_NAME_MAX + 2];
            target[0] = '/';
            memcpy(target + 1, backup[index].data, old->size);
            target[old->size + 1] = 0;
            success = fs_symlink(target, old->name);
        } else {
            static const uint8_t empty = 0;
            const void *data = old->size ? (const void *)backup[index].data : &empty;
            success = fs_write(old->name, data, old->size);
        }
        if (success) {
            fs_chmod(old->name, old->flags & 0777);
            fs_chown(old->name, old->owner);
        }
    }
    release_legacy_backup(backup);
    return success;
}

bool fs_format(void) {
    return format_v2();
}

bool fs_init(void) {
    mounted = false;
    current_uid = 0;
    current_privileged = true;
    entries = heap_calloc(FS_MAX_ENTRIES, sizeof(FsEntry));
    if (!entries) return false;
    if (!ata_read_sector(FS_SUPER_LBA, &superblock) || !magic_matches(&superblock)) {
        return format_v2();
    }
    if (superblock.version == FS_LEGACY_VERSION &&
        superblock.directory_lba == FS_DIRECTORY_LBA &&
        superblock.directory_sectors == FS_LEGACY_DIRECTORY_SECTORS &&
        superblock.data_lba == FS_LEGACY_DATA_LBA &&
        superblock.max_entries == FS_LEGACY_MAX_ENTRIES) {
        return migrate_legacy();
    }
    if (superblock.version != FS_VERSION || superblock.directory_lba != FS_DIRECTORY_LBA ||
        superblock.directory_sectors != FS_DIRECTORY_SECTORS ||
        superblock.data_lba != FS_DATA_LBA || superblock.max_entries != FS_MAX_ENTRIES) {
        return false;
    }
    for (uint32_t sector = 0; sector < FS_DIRECTORY_SECTORS; ++sector) {
        if (!ata_read_sector(FS_DIRECTORY_LBA + sector,
                             (uint8_t *)entries + sector * ATA_SECTOR_SIZE)) return false;
    }
    superblock.total_sectors = ata_sector_count();
    ata_write_sector(FS_SUPER_LBA, &superblock);
    mounted = true;
    return true;
}

bool fs_is_ready(void) {
    return mounted;
}

int fs_read_at(const char *name, uint64_t offset, void *buffer, size_t capacity) {
    if (!mounted || !buffer || !valid_name(name)) return -1;
    int index = resolve_entry(name);
    if (index < 0) return -1;
    FsEntry *entry = &entries[index];
    if (entry->type != FS_TYPE_FILE || !may_read(entry)) return -1;
    return read_entry_data(entry, offset, buffer, capacity);
}

int fs_read(const char *name, void *buffer, size_t capacity) {
    return fs_read_at(name, 0, buffer, capacity);
}

bool fs_write(const char *name, const void *data, size_t size) {
    if (!mounted || !data || !valid_name(name)) return false;
    int index = find_entry(name);
    if (index < 0) {
        index = allocate_entry(name, FS_TYPE_FILE, 0660);
        if (index < 0) return false;
    } else if (entries[index].type == FS_TYPE_SYMLINK) {
        index = resolve_entry(name);
        if (index < 0) return false;
    }
    if (entries[index].type != FS_TYPE_FILE || !may_write(&entries[index])) return false;
    return write_entry_data(&entries[index], index, data, size);
}

bool fs_write_at(const char *name, uint64_t offset, const void *data, size_t size) {
    if (!mounted || !data || !valid_name(name) || offset + size < offset) return false;
    int index = find_entry(name);
    if (index < 0) {
        index = allocate_entry(name, FS_TYPE_FILE, 0660);
        if (index < 0 || !save_entry(index)) return false;
    } else if (entries[index].type == FS_TYPE_SYMLINK) {
        index = resolve_entry(name);
        if (index < 0) return false;
    }
    FsEntry *entry = &entries[index];
    if (entry->type != FS_TYPE_FILE || !may_write(entry)) return false;
    uint64_t end = offset + size;
    uint64_t new_size = end > entry->size ? end : entry->size;
    uint64_t required64 = (new_size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (!required64) required64 = 1;
    if (required64 > 0xFFFFFFFFUL) return false;
    uint32_t required = (uint32_t)required64;
    uint8_t *sector = heap_alloc(ATA_SECTOR_SIZE);
    if (!sector) return false;

    if (entry->capacity_sectors < required) {
        uint32_t old_start = entry->start_lba;
        uint32_t old_capacity = entry->capacity_sectors;
        uint32_t replacement = next_data_lba();
        if ((uint64_t)replacement + required > ata_sector_count()) {
            heap_free(sector);
            return false;
        }
        for (uint32_t block = 0; block < required; ++block) {
            memset(sector, 0, ATA_SECTOR_SIZE);
            if (block < old_capacity && old_start &&
                !ata_read_sector(old_start + block, sector)) {
                heap_free(sector);
                return false;
            }
            if (!ata_write_sector(replacement + block, sector)) {
                heap_free(sector);
                return false;
            }
        }
        entry->start_lba = replacement;
        entry->capacity_sectors = required;
    }

    uint64_t gap = entry->size;
    while (gap < offset) {
        uint32_t block = (uint32_t)(gap / ATA_SECTOR_SIZE);
        size_t within = (size_t)(gap % ATA_SECTOR_SIZE);
        size_t chunk = (size_t)(offset - gap);
        if (chunk > ATA_SECTOR_SIZE - within) chunk = ATA_SECTOR_SIZE - within;
        if (!ata_read_sector(entry->start_lba + block, sector)) {
            heap_free(sector);
            return false;
        }
        memset(sector + within, 0, chunk);
        if (!ata_write_sector(entry->start_lba + block, sector)) {
            heap_free(sector);
            return false;
        }
        gap += chunk;
    }

    uint64_t position = offset;
    size_t consumed = 0;
    while (consumed < size) {
        uint32_t block = (uint32_t)(position / ATA_SECTOR_SIZE);
        size_t within = (size_t)(position % ATA_SECTOR_SIZE);
        size_t chunk = size - consumed;
        if (chunk > ATA_SECTOR_SIZE - within) chunk = ATA_SECTOR_SIZE - within;
        if (within || chunk != ATA_SECTOR_SIZE) {
            if (!ata_read_sector(entry->start_lba + block, sector)) {
                heap_free(sector);
                return false;
            }
            if (position > entry->size) {
                uint64_t sector_start = position - within;
                size_t zero_from = entry->size > sector_start ?
                                   (size_t)(entry->size - sector_start) : 0;
                if (zero_from < within) memset(sector + zero_from, 0, within - zero_from);
            }
        } else {
            memset(sector, 0, ATA_SECTOR_SIZE);
        }
        memcpy(sector + within, (const uint8_t *)data + consumed, chunk);
        if (!ata_write_sector(entry->start_lba + block, sector)) {
            heap_free(sector);
            return false;
        }
        position += chunk;
        consumed += chunk;
    }
    heap_free(sector);
    entry->size = new_size;
    entry->modified++;
    return save_entry(index);
}

bool fs_delete(const char *name) {
    if (!mounted || !valid_name(name)) return false;
    int index = find_entry(name);
    if (index < 0 || !may_write(&entries[index])) return false;
    if (entries[index].type == FS_TYPE_DIRECTORY) {
        size_t length = strlen(name);
        for (int candidate = 0; candidate < FS_MAX_ENTRIES; ++candidate) {
            if (!entries[candidate].used || candidate == index) continue;
            if (!strncmp(entries[candidate].name, name, length) &&
                entries[candidate].name[length] == '/') return false;
        }
    }
    memset(&entries[index], 0, sizeof(FsEntry));
    return save_entry(index);
}

bool fs_mkdir(const char *name, uint16_t mode) {
    if (!mounted || !valid_name(name) || (mode & ~0777)) return false;
    int index = allocate_entry(name, FS_TYPE_DIRECTORY, mode ? mode : 0777);
    return index >= 0 && save_entry(index);
}

bool fs_symlink(const char *target, const char *name) {
    if (!mounted || !valid_symlink_target(target) || !valid_name(name) ||
        find_entry(name) >= 0) {
        return false;
    }
    size_t length = strlen(target);
    if (!fs_write(name, target, length)) return false;
    int index = find_entry(name);
    if (index < 0) return false;
    entries[index].type = FS_TYPE_SYMLINK;
    entries[index].flags = 0777;
    return save_entry(index);
}

int fs_readlink(const char *name, char *target, size_t capacity) {
    if (!mounted || !target || !capacity || !valid_name(name)) return -1;
    int index = find_entry(name);
    if (index < 0 || entries[index].type != FS_TYPE_SYMLINK || !may_read(&entries[index])) {
        return -1;
    }
    return read_entry_data(&entries[index], 0, target, capacity);
}

bool fs_path_info(const char *name, FsFileInfo *info, bool follow_symlink) {
    if (!mounted || !info || !valid_name(name)) return false;
    int index = follow_symlink ? resolve_entry(name) : find_entry(name);
    if (index < 0) return false;
    strcpy(info->name, entries[index].name);
    info->size = entries[index].size;
    info->flags = entry_mode(&entries[index]);
    info->owner = entries[index].owner;
    info->type = entries[index].type;
    return true;
}

bool fs_chmod(const char *name, uint16_t mode) {
    if (!mounted || !valid_name(name) || (mode & ~0777)) return false;
    int index = find_entry(name);
    if (index < 0 || (!current_privileged && current_uid != entries[index].owner)) return false;
    entries[index].flags = (entries[index].flags & ~0777) | mode;
    return save_entry(index);
}

bool fs_chown(const char *name, uint32_t owner) {
    if (!mounted || !current_privileged || !valid_name(name)) return false;
    int index = find_entry(name);
    if (index < 0) return false;
    entries[index].owner = owner;
    return save_entry(index);
}

void fs_set_identity(uint32_t uid, bool privileged) {
    current_uid = uid;
    current_privileged = privileged;
}

uint32_t fs_current_uid(void) {
    return current_uid;
}

int fs_file_count(void) {
    if (!mounted) return 0;
    int count = 0;
    for (int index = 0; index < FS_MAX_ENTRIES; ++index) {
        if (entries[index].used) count++;
    }
    return count;
}

bool fs_file_info(int visible_index, FsFileInfo *info) {
    if (!mounted || !info || visible_index < 0) return false;
    for (int index = 0; index < FS_MAX_ENTRIES; ++index) {
        if (!entries[index].used) continue;
        if (visible_index-- > 0) continue;
        strcpy(info->name, entries[index].name);
        info->size = entries[index].size;
        info->flags = entry_mode(&entries[index]);
        info->owner = entries[index].owner;
        info->type = entries[index].type;
        return true;
    }
    return false;
}
