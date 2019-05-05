#include <jsmn.h>
#include "custom_fields.h"
#include "id_hash_map.h"
#include "json.h"
#include "lazy_array.h"

using Custom_Field_Handle = Entity_Handle<Custom_Field>;

static const Custom_Field_Handle NULL_CUSTOM_FIELD_HANDLE(-1);
static Lazy_Array<Custom_Field, 32> custom_fields{};
static Id_Hash_Map<Custom_Field_Id, s32, -1> id_to_custom_field = {};
static Id_Hash_Map<User_Id, bool, false> id_to_is_custom_field_requested{};
static Temporary_List<Custom_Field_Id> custom_field_request_queue{};

static void process_custom_field(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Custom_Field_Handle custom_field_handle = Custom_Field_Handle(custom_fields.length);
    Custom_Field* custom_field = &custom_fields[custom_fields.length++];

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

    id_hash_map_put(&id_to_custom_field, (s32) custom_field_handle, custom_field->id, custom_field->id_hash);
}

void init_custom_field_storage() {
    id_hash_map_init(&id_to_custom_field);
    id_hash_map_init(&id_to_is_custom_field_requested);
}

void process_custom_fields_data(char* json, u32 data_size, jsmntok_t*&token) {
    lazy_array_reserve_n_values(custom_fields, data_size);

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_custom_field(json, token);
    }
}

bool is_custom_field_requested(Custom_Field_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return id_hash_map_get(&id_to_is_custom_field_requested, id, id_hash);
}

void mark_custom_field_as_requested(Custom_Field_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    id_hash_map_put(&id_to_is_custom_field_requested, true, id, id_hash);
}

void try_queue_custom_field_info_request(Custom_Field_Id id, u32 id_hash) {
    if (!is_custom_field_requested(id, id_hash)) {
        list_add(&custom_field_request_queue, id);
    }
}

Temporary_List<Custom_Field_Id > get_and_clear_custom_field_request_queue() {
    Temporary_List<Custom_Field_Id> result = custom_field_request_queue;

    custom_field_request_queue = {};

    return result;
}

Custom_Field* find_custom_field_by_id(Custom_Field_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    Custom_Field_Handle handle(id_hash_map_get(&id_to_custom_field, id, id_hash));

    if (handle == NULL_CUSTOM_FIELD_HANDLE) {
        return NULL;
    }

    return &custom_fields[handle.value];
}
