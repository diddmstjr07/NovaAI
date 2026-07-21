#ifndef NOVA_FILESYSTEM_H
#define NOVA_FILESYSTEM_H

#include "types.h"

#define FS_NAME_MAX 95
#define FS_TYPE_FILE 1
#define FS_TYPE_DIRECTORY 2
#define FS_TYPE_SYMLINK 3

typedef struct {
    char name[FS_NAME_MAX + 1];
    uint64_t size;
    uint16_t flags;
    uint32_t owner;
    uint8_t type;
} FsFileInfo;

bool fs_init(void);
bool fs_is_ready(void);
bool fs_format(void);
int fs_read(const char *name, void *buffer, size_t capacity);
int fs_read_at(const char *name, uint64_t offset, void *buffer, size_t capacity);
bool fs_write(const char *name, const void *data, size_t size);
bool fs_write_at(const char *name, uint64_t offset, const void *data, size_t size);
bool fs_delete(const char *name);
bool fs_mkdir(const char *name, uint16_t mode);
bool fs_symlink(const char *target, const char *name);
int fs_readlink(const char *name, char *target, size_t capacity);
bool fs_path_info(const char *name, FsFileInfo *info, bool follow_symlink);
bool fs_chmod(const char *name, uint16_t mode);
bool fs_chown(const char *name, uint32_t owner);
void fs_set_identity(uint32_t uid, bool privileged);
uint32_t fs_current_uid(void);
int fs_file_count(void);
bool fs_file_info(int visible_index, FsFileInfo *info);

#endif
