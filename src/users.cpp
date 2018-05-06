#include "users.h"
#include "json.h"
#include "id_hash_map.h"

static User* users = NULL;
static u32 users_count = 0;

static Id_Hash_Map<User_Id, User*> id_to_user_map{};

static void process_users_data_object(char* json, jsmntok_t*&token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    User* user = &users[users_count++];

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_id8(json, next_token, user->id);
        } else if (json_string_equals(json, property_token, "firstName")) {
            json_token_to_string(json, next_token, user->firstName);
        } else if (json_string_equals(json, property_token, "lastName")) {
            json_token_to_string(json, next_token, user->lastName);
        } else {
            eat_json(token);
            token--;
        }
    }

    id_hash_map_put(&id_to_user_map, user, user->id, hash_id(user->id));
}

void process_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (users_count < data_size) {
        users = (User*) REALLOC(users, sizeof(User) * data_size);
    }

    if (id_to_user_map.table) {
        id_hash_map_destroy(&id_to_user_map);
    }

    id_hash_map_init(&id_to_user_map);

    users_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_users_data_object(json, token);
    }
}

User* find_user_by_id(User_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return id_hash_map_get(&id_to_user_map, id, id_hash);
}