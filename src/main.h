#pragma once

#include "common.h"
#include "rich_text.h"

bool init();

extern "C"
void loop();

extern "C"
void api_request_success(Request_Id request_id, char* content, u32 content_length, void* data);

extern "C"
void image_load_success(Request_Id request_id, u8* pixel_data, u32 width, u32 height);

enum View {
    View_Task_List,
    View_Inbox
};

struct Custom_Field_Value {
    Custom_Field_Id field_id;
    String value;
};

struct Task_Comment {
    Rich_Text text{};
    User_Id author;
};

// TODO is this just another form of Folder_Task? We could merge them
struct Task {
    Task_Id id;

    String title;
    String permalink;
    Rich_Text description{};

    Custom_Status_Id status_id;

    Array<Custom_Field_Value> custom_field_values{};
    Array<Custom_Field_Id> inherited_custom_fields{};
    Array<Folder_Id> parents{};
    Array<Folder_Id> super_parents{};
    Array<User_Id> authors{};
    Array<User_Id> assignees{};
    Array<Task_Comment> comments{};
};

// TODO make this a define?
extern const Request_Id NO_REQUEST;

// TODO we could actually have a struct which encompasses Request_Id, started_at and finished_at
extern Request_Id folder_tree_request;
extern Request_Id folder_header_request;
extern Request_Id folder_contents_request;
extern Request_Id task_request;
extern Request_Id contacts_request;
extern Request_Id account_request;
extern Request_Id workflows_request;

extern u32 started_showing_main_ui_at;

extern u32 started_loading_folder_contents_at;
extern u32 finished_loading_folder_contents_at;
extern u32 finished_loading_folder_header_at;

extern u32 started_loading_task_at;
extern u32 finished_loading_task_at;

extern u32 started_loading_statuses_at;
extern u32 finished_loading_statuses_at;

extern u32 started_loading_users_at;
extern u32 finished_loading_users_at;

extern bool custom_statuses_were_loaded;

extern u32 tick;

extern View current_view;
extern Task current_task;
extern Task_Id selected_folder_task_id;

void request_task_by_task_id(Task_Id task_id);
void add_assignee_to_task(Task_Id task_id, User_Id user_id);
void add_parent_folder(Task_Id task_id, Folder_Id folder_id);
void remove_assignee_from_task(Task_Id task_id, User_Id user_id);
void remove_parent_folder(Task_Id task_id, Folder_Id folder_id);
void set_task_status(Task_Id task_id, Custom_Status_Id status_id);
void select_and_request_folder_by_id(Folder_Id id);
void request_folder_children_for_folder_tree(Folder_Id folder_id);
void request_multiple_folders(Array<Folder_Id> folders);
void mark_notification_as_read(Inbox_Notification_Id notification_id);

// TODO those probably leak both on desktop and web
extern "C" char* handle_clipboard_copy();
extern "C" void handle_clipboard_paste(char* data, u32 data_length);

PRINTLIKE(2, 3) void image_request(Request_Id& request_id, const char* format, ...);

// TODO move this into imgui extension file?
namespace ImGui {
    void FadeInOverlay(float alpha);
}