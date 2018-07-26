#include <jsmn.h>
#include "json.h"
#include "id_hash_map.h"
#include "workflows.h"

Array<Workflow> workflows{};

static Array<Custom_Status> custom_statuses{};
static Id_Hash_Map<Custom_Status_Id, Custom_Status*> id_to_custom_status = {};

static u32 color_name_to_color_argb(String &color_name) {
    char c = *color_name.start;

    switch (color_name.length) {
        // Red
        case 3: return 0xFFE91E63;

        case 4: {
            if (c == 'B'/*lue*/) return 0xFF2196F3; else
            if (c == 'G'/*ray*/) return 0xFF9E9E9E;

            break;
        }

        case 5: {
            if (c == 'B'/*rown*/) return 0xFF795548; else
            if (c == 'G'/*reen*/) return 0xFF8BC34A;

            break;
        }

        case 6: {
            if (c == 'Y'/*ellow*/) return 0xFFFFEB3B; else
            if (c == 'P'/*urple*/) return 0xFF9C27B0; else
            if (c == 'O'/*range*/) return 0xFFFF9800; else
            if (c == 'I'/*ndigo*/) return 0xFF673AB7;

            break;
        }

        case 8: {
            c = *(color_name.start + 4);

            if (c == /*Dark*/'B'/*lue*/) return 0xFF3F51B5; else
            if (c == /*Dark*/'C'/*yan*/) return 0xFF009688;

            break;
        }

            // Turquoise
        case 9: return 0xFF00BCD4;

            // YellowGreen
        case 11: return 0xFFCDDC39;
    }

    printf("Got unknown color: %.*s\n", color_name.length, color_name.start);
    return 0xFF000000;
}

static Status_Group status_group_name_to_status_group(String& name) {
    if (name.length < 2) {
        return Status_Group_Invalid;
    }

    switch (*name.start) {
        case 'A'/*ctive*/: return Status_Group_Active;
        case 'C': {
            switch (*(name.start + 1)) {
                case /*C*/'o'/*mpleted*/: return Status_Group_Completed;
                case /*C*/'a'/*ncelled*/: return Status_Group_Cancelled;
            }

            break;
        }

        case 'D'/*eferred*/: return Status_Group_Deferred;
    }

    return Status_Group_Invalid;
}

static u32 status_group_to_color(Status_Group group) {
    switch (group) {
        case Status_Group_Active: return 0xFF2196F3;
        case Status_Group_Completed: return 0xFF8BC34A;
        case Status_Group_Cancelled:
        case Status_Group_Deferred:
            return 0xFF9E9E9E;

        default: return 0xFF000000;
    }
}

static void process_custom_status(Workflow* workflow, char* json, jsmntok_t*& token, u32 natural_index) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Custom_Status* custom_status = &custom_statuses[custom_statuses.length++];
    custom_status->natural_index = natural_index;
    custom_status->workflow = workflow;
    custom_status->is_hidden = false;

    // TODO unused
    bool is_standard = false;

    custom_status->color = 0;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, custom_status->id);
        } else if (json_string_equals(json, property_token, "name")) {
            json_token_to_string(json, next_token, custom_status->name);
        } else if (json_string_equals(json, property_token, "standard")) {
            is_standard = *(json + next_token->start) == 't';
        } else if (json_string_equals(json, property_token, "hidden")) {
            custom_status->is_hidden = *(json + next_token->start) == 't';
        } else if (json_string_equals(json, property_token, "color")) {
            String color_name;
            json_token_to_string(json, next_token, color_name);

            custom_status->color = argb_to_agbr(color_name_to_color_argb(color_name));
        } else if (json_string_equals(json, property_token, "group")) {
            String group_name;
            json_token_to_string(json, next_token, group_name);

            custom_status->group = status_group_name_to_status_group(group_name);
        } else {
            eat_json(token);
            token--;
        }
    }

    if (!custom_status->color) {
        custom_status->color = argb_to_agbr(status_group_to_color(custom_status->group));
    }

    custom_status->id_hash = hash_id(custom_status->id);

    id_hash_map_put(&id_to_custom_status, custom_status, custom_status->id, custom_status->id_hash);
}

void process_workflows_data(char* json, u32 data_size, jsmntok_t*&token) {
    jsmntok_t* json_start = token;

    u32 total_statuses = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "customStatuses")) {
                total_statuses += next_token->size;
            }

            eat_json(token);
            token--;
        }
    }

    token = json_start;

    // TODO hacky, we need to clear the map when it's populated too
    if (id_to_custom_status.size == 0) {
        id_hash_map_init(&id_to_custom_status);
    }

    if (workflows.length < data_size) {
        workflows.data = (Workflow*) REALLOC(workflows.data, sizeof(Workflow) * data_size);
    }

    if (custom_statuses.length < total_statuses) {
        custom_statuses.data = (Custom_Status*) REALLOC(custom_statuses.data, sizeof(Custom_Status) * total_statuses);
    }

    workflows.length = 0;
    custom_statuses.length = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        Workflow* workflow = &workflows[workflows.length++];

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "id")) {
                json_token_to_right_part_of_id16(json, next_token, workflow->id);
            } else if (json_string_equals(json, property_token, "name")) {
                json_token_to_string(json, next_token, workflow->name);
            } else if (json_string_equals(json, property_token, "customStatuses")) {
                assert(next_token->type == JSMN_ARRAY);

                token++;

                workflow->statuses.data = &custom_statuses[custom_statuses.length];
                workflow->statuses.length = (u32) next_token->size;

                for (u32 status_index = 0; status_index < next_token->size; status_index++) {
                    process_custom_status(workflow, json, token, status_index);
                }

                token--;
            } else {
                eat_json(token);
                token--;
            }
        }
    }
}

Custom_Status* find_custom_status_by_id(Custom_Status_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return id_hash_map_get(&id_to_custom_status, id, id_hash);
}