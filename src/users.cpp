#include "users.h"
#include "json.h"
#include "id_hash_map.h"

Array<User> users{};
Array<User> suggested_users{};

User* this_user = NULL;

static Id_Hash_Map<User_Id, User*> id_to_user_map{};

static User* process_users_data_object(Array<User>& target_users, char* json, jsmntok_t*&token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    User* user = &target_users[target_users.length++];

    user->avatar_request_id = NO_REQUEST;
    user->avatar = {};

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
            // TODO This can and will happen twice because this user can occur
            // TODO     both in the suggested list and in the contacts list
            // TODO     a good solution is using a centralized 'truth' source
            // TODO     for all users
            if (!this_user && *(json + next_token->start) == 't') {
                this_user = user;
            }
        } else {
            eat_json(token);
            token--;
        }
    }

    return user;
}

void process_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (users.length < data_size) {
        users.data = (User*) REALLOC(users.data, sizeof(User) * data_size);
    }

    if (id_to_user_map.table) {
        id_hash_map_destroy(&id_to_user_map);
    }

    id_hash_map_init(&id_to_user_map);

    users.length = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        User* user = process_users_data_object(users, json, token);

        id_hash_map_put(&id_to_user_map, user, user->id, hash_id(user->id));;
    }
}

void process_suggested_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (suggested_users.length < data_size) {
        suggested_users.data = (User*) REALLOC(suggested_users.data, sizeof(User) * data_size);
    }

    suggested_users.length = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_users_data_object(suggested_users, json, token);
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
    for (User* it = suggested_users.data; it != suggested_users.data + suggested_users.length; it++) {
        if (it->avatar_request_id == avatar_request_id) {
            return it;
        }
    }

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

    return id_hash_map_get(&id_to_user_map, id, id_hash);
}