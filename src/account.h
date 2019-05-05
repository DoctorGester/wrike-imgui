#pragma once

#include <jsmn.h>
#include "id_hash_map.h"

#define NO_ACCOUNT -1

struct Account {
    Account_Id id = NO_ACCOUNT;
};

extern Account account;

void process_account_data(char *json, u32 data_size, jsmntok_t *&token);