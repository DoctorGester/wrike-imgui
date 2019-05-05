#include "json.h"
#include "main.h"
#include "account.h"

Account account{};

void process_account_data(char *json, u32 data_size, jsmntok_t *&token) {
    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "id")) {
                json_token_to_id8(json, next_token, account.id);
            } else {
                eat_json(token);
                token--;
            }
        }
    }
}