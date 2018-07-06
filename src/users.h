#include <jsmn.h>
#include "common.h"
#include "temporary_storage.h"
#include "main.h"

#pragma once

struct User {
    User_Id id;
    String first_name;
    String last_name;
    String avatar_url;

    Request_Id avatar_request_id;
    Memory_Image avatar{};
    u32 avatar_loaded_at;
};

extern List<User> users;
extern List<User> suggested_users;

extern User* this_user;

void process_users_data(char* json, u32 data_size, jsmntok_t*& token);
void process_suggested_users_data(char* json, u32 data_size, jsmntok_t*&token);

User* find_user_by_id(User_Id id, u32 id_hash = 0);
User* find_user_by_avatar_request_id(Request_Id avatar_request_id);

bool check_and_request_user_avatar_if_necessary(User* user);

inline String full_user_name_to_temporary_string(User* user) {
    return tprintf("%.*s %.*s",
                   user->first_name.length, user->first_name.start,
                   user->last_name.length, user->last_name.start
    );
}