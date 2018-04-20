#pragma once

#include <jsmn.h>
#include "hash_map.h"

struct Custom_Field {
    Id16 id;
    String title;
    u32 id_hash;
};

struct Account {
    Id8 id;
};

extern Account* accounts;
extern u32 accounts_count;

extern Hash_Map<Custom_Field*> id_to_custom_field;

void process_accounts_data(char* json, u32 data_size, jsmntok_t*&token);