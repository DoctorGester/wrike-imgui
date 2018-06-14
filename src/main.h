#pragma once

#include "common.h"
#include "rich_text.h"

bool init();

extern "C"
void loop();

typedef signed long Request_Id;

extern "C"
void api_request_success(Request_Id request_id, char* content, u32 content_length);

#define PRINTLIKE(string_index, first_to_check) __attribute__((__format__ (__printf__, string_index, first_to_check)))

struct Custom_Field_Value {
    Custom_Field_Id field_id;
    String value;
};

// TODO is this just another form of Folder_Task? We could merge them
struct Task {
    Task_Id id;

    String title;
    String permalink;
    Rich_Text_String* description = NULL;
    u32 description_strings = 0;
    String description_text{};

    Custom_Status_Id status_id;

    List<Custom_Field_Value> custom_field_values{};
    List<Custom_Field_Id> inherited_custom_fields{};
    List<Folder_Id> parents{};
    List<User_Id> authors{};
    List<User_Id> assignees{};
};

// TODO make this a define?
extern const Request_Id NO_REQUEST;

// TODO we could actually have a struct which encompasses Request_Id, started_at and finished_at
extern Request_Id folder_tree_request;
extern Request_Id folder_header_request;
extern Request_Id folder_contents_request;
extern Request_Id task_request;
extern Request_Id contacts_request;
extern Request_Id accounts_request;
extern Request_Id workflows_request;

extern u32 started_loading_folder_contents_at;
extern u32 finished_loading_folder_contents_at;
extern u32 finished_loading_folder_header_at;

extern u32 started_loading_task_at;
extern u32 finished_loading_task_at;

extern u32 started_loading_statuses_at;
extern u32 finished_loading_statuses_at;

extern u32 started_loading_users_at;
extern u32 finished_loading_users_at;

extern bool had_last_selected_folder_so_doesnt_need_to_load_the_root_folder;
extern bool custom_statuses_were_loaded;

extern u32 tick;

extern Task current_task;
extern Task_Id selected_folder_task_id;

void request_task_by_task_id(Task_Id task_id);
PRINTLIKE(2, 3) void get_request(Request_Id& request_id, const char* format, ...);

// TODO move this into imgui extension file?
namespace ImGui {
    void LoadingIndicator(u32 started_showing_at);
    void Image(Memory_Image& image);
}