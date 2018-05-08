#include <jsmn.h>
#include "common.h"
#include "temporary_storage.h"

struct User {
    User_Id id;
    String first_name;
    String last_name;
};

void process_users_data(char* json, u32 data_size, jsmntok_t*& token);
User* find_user_by_id(User_Id id, u32 id_hash = 0);

inline String full_user_name_to_temporary_string(User* user) {
    String result;
    result.length = user->first_name.length + user->last_name.length + 1;
    result.start = (char*) talloc(result.length);

    memcpy(result.start, user->first_name.start, user->first_name.length);
    memcpy(result.start + user->first_name.length + 1, user->last_name.start, user->last_name.length);

    result.start[user->first_name.length] = ' ';

    return result;
}