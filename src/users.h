#include <jsmn.h>
#include "common.h"
#include "temporary_storage.h"

struct User {
    User_Id id;
    String firstName;
    String lastName;
};

void process_users_data(char* json, u32 data_size, jsmntok_t*& token);
User* find_user_by_id(User_Id id, u32 id_hash = 0);

inline String full_user_name_to_temporary_string(User* user) {
    String result;
    result.length = user->firstName.length + user->lastName.length + 1;
    result.start = (char*) talloc(result.length);

    memcpy(result.start, user->firstName.start, user->firstName.length);
    memcpy(result.start + user->firstName.length + 1, user->lastName.start, user->lastName.length);

    result.start[user->firstName.length] = ' ';

    return result;
}