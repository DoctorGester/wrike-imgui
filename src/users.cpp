#include "users.h"
#include "json.h"
#include "id_hash_map.h"

Lazy_Array<User, 32> users{};
Array<User_Handle> suggested_users{};

Temporary_List<User_Id> user_request_queue{};

User_Handle this_user = NULL_USER_HANDLE;

// <User_Id, User_Handle, NULL_USER_HANDLE>
static Id_Hash_Map<User_Id, s32, -1> id_to_user_map{};

// TODO an interesting thought:
// TODO instead of storing this, create an unloaded user
// TODO + a bool indicating that this user is loaded
// TODO whenever that user is requested
// TODO we could have a general facility which requests lists of users
// TODO to be loaded at the end of a frame
static Id_Hash_Map<User_Id, bool, false> id_to_is_user_requested{};

static User_Handle process_users_data_object(char* json, jsmntok_t*&token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    User_Handle user_handle = User_Handle(users.length);
    User* user = &users[users.length++];

    user->avatar_request_id = NO_REQUEST;
    user->avatar = {};
    user->loaded_at = tick;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_id8(json, next_token, user->id);
        } else if (json_string_equals(json, property_token, "firstName")) {
            json_token_to_string(json, next_token, user->first_name);
        } else if (json_string_equals(json, property_token, "lastName")) {
            json_token_to_string(json, next_token, user->last_name);
        } else if (json_string_equals(json, property_token, "avatarUrl")) {
            json_token_to_string(json, next_token, user->avatar_url);
        } else if (json_string_equals(json, property_token, "me")) {
            if (this_user == NULL_USER_HANDLE && *(json + next_token->start) == 't') {
                this_user = user_handle;
            }
        } else {
            eat_json(token);
            token--;
        }
    }

    id_hash_map_put(&id_to_user_map, (s32) user_handle, user->id, hash_id(user->id));

    return user_handle;
}

void init_user_storage() {
    id_hash_map_init(&id_to_is_user_requested);
    id_hash_map_init(&id_to_user_map);
}

void process_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    lazy_array_reserve_n_values(users, data_size);

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_users_data_object(json, token);
    }
}

void process_suggested_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (suggested_users.length < data_size) {
        suggested_users.data = (User_Handle*) REALLOC(suggested_users.data, sizeof(User_Handle) * data_size);
    }

    suggested_users.length = 0;

    lazy_array_reserve_n_values(users, data_size);

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        suggested_users[suggested_users.length++] = process_users_data_object(json, token);
    }
}

bool check_and_request_user_avatar_if_necessary(User* user) {
    if (!user->avatar.texture_id) {
        if (user->avatar_request_id == NO_REQUEST) {
            image_request(user->avatar_request_id, "%.*s", user->avatar_url.length, user->avatar_url.start);
        }

        return false;
    }

    return true;
}

// Naive and slow, don't use too often
User* find_user_by_avatar_request_id(Request_Id avatar_request_id) {
    for (User* it = users.data; it != users.data + users.length; it++) {
        if (it->avatar_request_id == avatar_request_id) {
            return it;
        }
    }

    return NULL;
}

User* find_user_by_id(User_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    User_Handle handle = (User_Handle) id_hash_map_get(&id_to_user_map, id, id_hash);

    if (handle != NULL_USER_HANDLE) {
        return get_user_by_handle(handle);
    }

    return NULL;
}

User_Handle find_user_handle_by_id(User_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return (User_Handle) id_hash_map_get(&id_to_user_map, id, id_hash);
}

bool is_user_requested(User_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return id_hash_map_get(&id_to_is_user_requested, id, id_hash);
}

void mark_user_as_requested(User_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    id_hash_map_put(&id_to_is_user_requested, true, id, id_hash);
}

User* get_user_by_handle(User_Handle handle) {
    return users.data + handle.value;
}

void try_queue_user_info_request(User_Id id, u32 id_hash) {
    if (!is_user_requested(id, id_hash)) {
        list_add(&user_request_queue, id);
    }
}

Temporary_List<User_Id> get_and_clear_user_request_queue() {
    Temporary_List<User_Id> result = user_request_queue;

    user_request_queue = {};

    return result;
}
