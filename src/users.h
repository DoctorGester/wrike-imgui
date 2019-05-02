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

    u32 loaded_at;

    Request_Id avatar_request_id;
    Memory_Image avatar{};
    u32 avatar_loaded_at;
};

struct User_Handle {
    s32 value;

    User_Handle(){};

    explicit User_Handle(s32 v) : value(v) {};

    explicit operator s32() const {
        return value;
    }

    bool operator ==(const User_Handle& handle) const {
        return value == handle.value;
    }

    bool operator !=(const User_Handle& handle) const {
        return value != handle.value;
    }
};

extern Lazy_Array<User, 32> users;
extern Array<User_Handle> suggested_users;

extern User_Handle this_user;

const User_Handle NULL_USER_HANDLE = User_Handle(-1);

void init_user_storage();
void process_users_data(char* json, u32 data_size, jsmntok_t*& token);
void process_suggested_users_data(char* json, u32 data_size, jsmntok_t*&token);

bool is_user_requested(User_Id id, u32 id_hash = 0);
User_Handle find_user_handle_by_id(User_Id id, u32 id_hash = 0);
User* find_user_by_id(User_Id id, u32 id_hash = 0);
User* find_user_by_avatar_request_id(Request_Id avatar_request_id);
User* get_user_by_handle(User_Handle handle);

void mark_user_as_requested(User_Id id, u32 id_hash = 0);

bool check_and_request_user_avatar_if_necessary(User* user);

void try_queue_user_info_request(User_Id id);
Array<User_Id> get_and_clear_user_request_queue();

inline String full_user_name_to_temporary_string(User* user) {
    // This function used tprintf("%.*s %.*s", ...) earlier, but turns out snprintf is ridiculously slow
    String result;
    result.length = user->first_name.length + user->last_name.length + 1;
    result.start = (char*) talloc(result.length);

    memcpy(result.start, user->first_name.start, user->first_name.length);
    memcpy(result.start + user->first_name.length + 1, user->last_name.start, user->last_name.length);

    result.start[user->first_name.length] = ' ';

    return result;
}