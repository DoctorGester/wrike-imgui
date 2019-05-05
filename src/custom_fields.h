#include "common.h"

enum Custom_Field_Type {
    Custom_Field_Type_None,
    Custom_Field_Type_Text,
    Custom_Field_Type_DropDown,
    Custom_Field_Type_Numeric,
    Custom_Field_Type_Currency,
    Custom_Field_Type_Percentage,
    Custom_Field_Type_Date,
    Custom_Field_Type_Duration,
    Custom_Field_Type_Checkbox,
    Custom_Field_Type_Contacts
};

struct Custom_Field {
    Custom_Field_Id id;
    u32 id_hash;
    String title;
    Custom_Field_Type type;
};

void init_custom_field_storage();
void process_custom_fields_data(char* json, u32 data_size, jsmntok_t*&token);
bool is_custom_field_requested(Custom_Field_Id id, u32 id_hash = 0);
void try_queue_custom_field_info_request(Custom_Field_Id id, u32 id_hash = 0);
void mark_custom_field_as_requested(Custom_Field_Id id, u32 id_hash = 0);
Temporary_List<Custom_Field_Id > get_and_clear_custom_field_request_queue();
Custom_Field* find_custom_field_by_id(Custom_Field_Id id, u32 id_hash = 0);