#include "deb.h"
#include "filesystem.h"
#include "heap.h"
#include "runtime.h"

#define AR_HEADER_SIZE 60
#define TAR_BLOCK_SIZE 512

extern const uint8_t builtin_test_deb_start[];
extern const uint8_t builtin_test_deb_end[];

static bool all_zero(const uint8_t *bytes, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        if (bytes[index]) return false;
    }
    return true;
}

static bool parse_number(const uint8_t *text, size_t length, uint32_t base,
                         size_t *value) {
    size_t result = 0;
    bool found = false;
    for (size_t index = 0; index < length; ++index) {
        uint8_t character = text[index];
        if (character == 0 || character == ' ') {
            if (found) break;
            continue;
        }
        if (character < '0' || character >= '0' + base) return false;
        size_t digit = character - '0';
        if (result > ((size_t)-1 - digit) / base) return false;
        result = result * base + digit;
        found = true;
    }
    *value = result;
    return found;
}

static void ar_name(const uint8_t *header, char output[17]) {
    size_t length = 16;
    while (length && (header[length - 1] == ' ' || header[length - 1] == '/')) --length;
    memcpy(output, header, length);
    output[length] = 0;
}

static bool package_path(const uint8_t *source, size_t source_length,
                         char output[FS_NAME_MAX + 1]) {
    size_t begin = 0;
    while (begin < source_length && source[begin] == '/') ++begin;
    while (source_length >= begin + 2 && source[begin] == '.' &&
           source[begin + 1] == '/') begin += 2;
    while (source_length > begin &&
           (source[source_length - 1] == 0 || source[source_length - 1] == '/')) {
        --source_length;
    }
    size_t length = source_length - begin;
    if (!length || length > FS_NAME_MAX) return false;
    size_t segment_start = 0;
    for (size_t index = 0; index < length; ++index) {
        uint8_t character = source[begin + index];
        if (character < 32 || character > 126) return false;
        if (character == '/') {
            if (index == segment_start ||
                (index - segment_start == 2 && source[begin + segment_start] == '.' &&
                 source[begin + segment_start + 1] == '.')) return false;
            segment_start = index + 1;
        }
    }
    if (length - segment_start == 2 && source[begin + segment_start] == '.' &&
        source[begin + segment_start + 1] == '.') return false;
    memcpy(output, source + begin, length);
    output[length] = 0;
    return true;
}

static bool ensure_parent_directories(char *path) {
    for (size_t index = 0; path[index]; ++index) {
        if (path[index] != '/') continue;
        path[index] = 0;
        FsFileInfo info;
        bool exists = fs_path_info(path, &info, false);
        if ((!exists && !fs_mkdir(path, 0755)) ||
            (exists && info.type != FS_TYPE_DIRECTORY)) {
            path[index] = '/';
            return false;
        }
        path[index] = '/';
    }
    return true;
}

static bool install_tar(const uint8_t *archive, size_t size, DebInstallResult *result) {
    size_t offset = 0;
    while (offset + TAR_BLOCK_SIZE <= size) {
        const uint8_t *header = archive + offset;
        if (all_zero(header, TAR_BLOCK_SIZE)) return true;
        size_t file_size;
        if (!parse_number(header + 124, 12, 8, &file_size)) return false;
        size_t stored_checksum;
        if (!parse_number(header + 148, 8, 8, &stored_checksum)) return false;
        size_t checksum = 0;
        for (size_t index = 0; index < TAR_BLOCK_SIZE; ++index) {
            checksum += index >= 148 && index < 156 ? ' ' : header[index];
        }
        if (checksum != stored_checksum) return false;

        size_t name_length = 0;
        while (name_length < 100 && header[name_length]) ++name_length;
        char path[FS_NAME_MAX + 1];
        if (!package_path(header, name_length, path) || !ensure_parent_directories(path)) {
            return false;
        }
        size_t data_offset = offset + TAR_BLOCK_SIZE;
        if (file_size > size - data_offset) return false;
        uint8_t type = header[156];
        FsFileInfo existing;
        bool exists = fs_path_info(path, &existing, false);
        if (type == '5') {
            if ((!exists && !fs_mkdir(path, 0755)) ||
                (exists && existing.type != FS_TYPE_DIRECTORY)) return false;
            result->directories++;
        } else if (type == '2') {
            size_t target_length = 0;
            while (target_length < 100 && header[157 + target_length]) ++target_length;
            bool absolute = target_length && header[157] == '/';
            char normalized[FS_NAME_MAX + 1];
            char target[FS_NAME_MAX + 2];
            if (!package_path(header + 157, target_length, normalized)) return false;
            if (absolute) {
                target[0] = '/';
                strcpy(target + 1, normalized);
            } else {
                strcpy(target, normalized);
            }
            if (exists && existing.type != FS_TYPE_SYMLINK) return false;
            if (exists && !fs_delete(path)) return false;
            if (!fs_symlink(target, path)) return false;
            result->symlinks++;
        } else if (type == 0 || type == '0') {
            if (exists && existing.type != FS_TYPE_FILE) return false;
            if (!fs_write(path, archive + data_offset, file_size)) return false;
            result->files++;
        }
        size_t padded = (file_size + TAR_BLOCK_SIZE - 1) & ~(TAR_BLOCK_SIZE - 1);
        if (padded > size - data_offset) return false;
        offset = data_offset + padded;
    }
    return false;
}

bool deb_install(const void *package_data, size_t size, DebInstallResult *result) {
    const uint8_t *package = package_data;
    DebInstallResult local;
    memset(&local, 0, sizeof(local));
    if (!package || size < 8 || memcmp(package, "!<arch>\n", 8)) return false;
    bool version_ok = false;
    const uint8_t *data_tar = NULL;
    size_t data_tar_size = 0;
    size_t offset = 8;
    while (offset + AR_HEADER_SIZE <= size) {
        const uint8_t *header = package + offset;
        if (header[58] != '`' || header[59] != '\n') return false;
        size_t member_size;
        if (!parse_number(header + 48, 10, 10, &member_size)) return false;
        size_t payload_offset = offset + AR_HEADER_SIZE;
        if (member_size > size - payload_offset) return false;
        char name[17];
        ar_name(header, name);
        if (!strcmp(name, "debian-binary")) {
            version_ok = member_size == 4 && !memcmp(package + payload_offset, "2.0\n", 4);
        } else if (!strcmp(name, "data.tar")) {
            data_tar = package + payload_offset;
            data_tar_size = member_size;
        }
        size_t next = payload_offset + member_size + (member_size & 1);
        if (next < payload_offset || next > size) return false;
        offset = next;
    }
    if (!version_ok || !data_tar) return false;
    if (!install_tar(data_tar, data_tar_size, &local)) return false;
    if (result) *result = local;
    return true;
}

bool deb_install_file(const char *name, DebInstallResult *result) {
    FsFileInfo info;
    if (!fs_path_info(name, &info, true) || info.type != FS_TYPE_FILE ||
        !info.size || info.size > 1024 * 1024) return false;
    uint8_t *package = heap_alloc(info.size);
    if (!package) return false;
    bool loaded = fs_read(name, package, info.size) == (int)info.size;
    bool installed = loaded && deb_install(package, info.size, result);
    heap_free(package);
    return installed;
}

bool deb_self_test(void) {
    DebInstallResult result;
    size_t size = (size_t)(builtin_test_deb_end - builtin_test_deb_start);
    if (!deb_install(builtin_test_deb_start, size, &result) ||
        result.files != 1 || result.directories != 2 || result.symlinks != 1) return false;
    char payload[24];
    static const char expected[] = "NovaOS .deb payload\n";
    int loaded = fs_read("opt/nova/current", payload, sizeof(payload));
    return loaded == (int)(sizeof(expected) - 1) &&
           !memcmp(payload, expected, sizeof(expected) - 1);
}
