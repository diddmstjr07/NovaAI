#ifndef NOVA_ACCOUNT_H
#define NOVA_ACCOUNT_H

#include "types.h"

#define ACCOUNT_NAME_MAX 23

typedef struct {
    char name[ACCOUNT_NAME_MAX + 1];
    uint32_t uid;
    bool administrator;
} AccountInfo;

bool account_init(void);
bool account_login(const char *name, const char *password);
bool account_create(const char *name, const char *password, bool administrator);
const char *account_current_name(void);
uint32_t account_current_uid(void);
bool account_current_is_admin(void);
int account_count(void);
bool account_info(int index, AccountInfo *info);

#endif
