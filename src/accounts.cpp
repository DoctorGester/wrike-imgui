#include "accounts.h"
#include "json.h"
#include "main.h"

// TODO those are per account, do we even care about that?
static Custom_Field* custom_fields = NULL;
static u32 custom_fields_count = 0;

Account* accounts = NULL;
u32 accounts_count = 0;

Id_Hash_Map<Custom_Field*> id_to_custom_field = { 0 };

static void process_custom_field(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Custom_Field* custom_field = &custom_fields[custom_fields_count++];

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, custom_field->id);
        } else if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, next_token, custom_field->title);
        } else if (json_string_equals(json, property_token, "type")) {

            if (json_string_equals(json, next_token, "Text")) {
                custom_field->type = Custom_Field_Type_Text;
            } else if (json_string_equals(json, next_token, "Numeric")) {
                custom_field->type = Custom_Field_Type_Numeric;
            } else if (json_string_equals(json, next_token, "DropDown")) {
                custom_field->type = Custom_Field_Type_DropDown;
            } else {
                // TODO all other cases, preferably with a more efficient comparison
                custom_field->type = Custom_Field_Type_None;
            }

        } else {
            eat_json(token);
            token--;
        }
    }

    custom_field->id_hash = hash_id(custom_field->id);

    id_hash_map_put(&id_to_custom_field, custom_field, custom_field->id_hash);
}

void process_accounts_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (accounts_count < data_size) {
        accounts = (Account*) realloc(accounts, sizeof(Account) * data_size);
    }

    accounts_count = 0;

    bool has_already_requested_a_folder = had_last_selected_folder_so_doesnt_need_to_load_the_root_folder;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        Account* account = &accounts[accounts_count++];

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "id")) {
                json_token_to_id8(json, next_token, account->id);
            } else if (!has_already_requested_a_folder && json_string_equals(json, property_token, "rootFolderId")) {
                void request_folder_contents(String &folder_id);

                String folder_id;
                json_token_to_string(json, next_token, folder_id);

                s32 id = 0;
                json_token_to_right_part_of_id16(json, next_token, id);

                request_folder_contents(folder_id);

                has_already_requested_a_folder = true;
            } else if (json_string_equals(json, property_token, "customFields")) {
                assert(next_token->type == JSMN_ARRAY);

                token++;

                // TODO broken with more than 1 account!
                custom_fields = (Custom_Field*) malloc(sizeof(Custom_Field) * next_token->size);
                id_hash_map_init(&id_to_custom_field, (u32) next_token->size);

                for (u32 field_index = 0; field_index < next_token->size; field_index++) {
                    process_custom_field(json, token);
                }

                token--;
            } else {
                eat_json(token);
                token--;
            }
        }
    }
}