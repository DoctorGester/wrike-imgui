#pragma once

#include <jsmn.h>
#include "id_hash_map.h"

enum Custom_Field_Type {
    Custom_Field_Type_None,
    Custom_Field_Type_Text,
    Custom_Field_Type_DropDown,
    Custom_Field_Type_Numeric,
    Custom_Field_Type_Currency,
    Custom_Field_Type_Percentage,
    Custom_Field_Type_Date,
    Custom_Field_Type_Duration,
    Custom_Field_Type_Checkbox,
    Custom_Field_Type_Contacts
};

struct Custom_Field {
    Custom_Field_Id id;
    u32 id_hash;
    String title;
    Custom_Field_Type type;
};

struct Account {
    Account_Id id;
};

extern Account* accounts;
extern u32 accounts_count;

extern Id_Hash_Map<Custom_Field_Id, Custom_Field*> id_to_custom_field;

void process_accounts_data(char* json, u32 data_size, jsmntok_t*&token);