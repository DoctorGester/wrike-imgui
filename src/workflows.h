
#include "common.h"

enum Status_Group {
    Status_Group_Invalid,
    Status_Group_Active,
    Status_Group_Completed,
    Status_Group_Deferred,
    Status_Group_Cancelled
};

struct Custom_Status {
    s32 id;
    String name;
    Status_Group group;
    u32 id_hash;
    u32 color;
    u32 natural_index;
};

void process_workflows_data(char* json, u32 data_size, jsmntok_t*&token);
Custom_Status* find_custom_status_by_id(Custom_Status_Id id, u32 id_hash = 0);