#include <jsmn.h>
#include "common.h"
#include "json.h"
#include "users.h"
#include "workflows.h"

enum Inbox_Notification_Type {
    Inbox_Notification_Type_Assign,
    Inbox_Notification_Type_Mention,
    Inbox_Notification_Type_Status
};

struct Comment_Notification {
    Comment_Id id;
    String text;
};

struct Status_Notification {
    Custom_Status_Id old_status;
    Custom_Status_Id new_status;
};

struct Inbox_Notification {
    Inbox_Notification_Id id;
    Inbox_Notification_Type type;
    User_Id author;
    bool unread;

    union {
        Comment_Notification comment;
        Status_Notification status;
    };
};

static u32 unread_notifications = 0;
static Array<Inbox_Notification> notifications{};

static void process_inbox_notification(Inbox_Notification* notification, char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, notification->id);
        } else if (json_string_equals(json, property_token, "authorUserId")) {
            json_token_to_id8(json, next_token, notification->author);
        } else if (json_string_equals(json, property_token, "unread")) {
            notification->unread = *(json + next_token->start) == 't';
        } else if (json_string_equals(json, property_token, "type")) {

            if (json_string_equals(json, next_token, "Assign")) {
                notification->type = Inbox_Notification_Type_Assign;
            } else if (json_string_equals(json, next_token, "Mention")) {
                notification->type = Inbox_Notification_Type_Mention;
            } else if (json_string_equals(json, next_token, "Status")) {
                notification->type = Inbox_Notification_Type_Status;
            }

        } else if (json_string_equals(json, property_token, "commentId")) {
            json_token_to_right_part_of_id16(json, next_token, notification->comment.id);
        } else if (json_string_equals(json, property_token, "commentText")) {
            json_token_to_string(json, next_token, notification->comment.text);

        } else if (json_string_equals(json, property_token, "oldCustomStatusId")) {
            json_token_to_right_part_of_id16(json, next_token, notification->status.old_status);
        } else if (json_string_equals(json, property_token, "newCustomStatusId")) {
            json_token_to_right_part_of_id16(json, next_token, notification->status.new_status);

        } else {
            eat_json(token);
            token--;
        }
    }
}

u32 get_unread_notifications() {
    return unread_notifications;
}

void draw_inbox() {
    ImGuiID task_list_id = ImGui::GetID("inbox");
    ImGui::BeginChildFrame(task_list_id, ImVec2(-1, -1));

    for (Inbox_Notification* it = notifications.data; it != notifications.data + notifications.length; it++) {
        User* author = find_user_by_id(it->author);

        if (!author) {
            // TODO @DataIntegrity should work
            continue;
        }

        String author_name = full_user_name_to_temporary_string(author);

        switch (it->type) {
            case Inbox_Notification_Type_Assign: {
                ImGui::Text("%.*s assigned a task to you",
                            author_name.length, author_name.start
                );

                break;
            }

            case Inbox_Notification_Type_Mention: {
                String comment_text = it->comment.text;

                ImGui::Text("%.*s mentioned you in task with text: %.*s",
                            author_name.length, author_name.start,
                            comment_text.length, comment_text.start
                );

                break;
            }

            case Inbox_Notification_Type_Status: {
                Custom_Status* old_status = find_custom_status_by_id(it->status.old_status);
                Custom_Status* new_status = find_custom_status_by_id(it->status.new_status);

                if (!old_status || !new_status) {
                    // TODO @DataIntegrity should work
                    continue;
                }

                ImGui::Text("%.*s changed task status from %.*s to %.*s",
                            author_name.length, author_name.start,
                            old_status->name.length, old_status->name.start,
                            new_status->name.length, new_status->name.start
                );

                break;
            }
        }
    }

    ImGui::EndChildFrame();
}

void process_inbox_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (notifications.length < data_size) {
        notifications.data = (Inbox_Notification*) REALLOC(notifications.data, sizeof(Inbox_Notification) * data_size);
    }

    notifications.length = 0;
    unread_notifications = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        Inbox_Notification* notification = &notifications[notifications.length++];

        process_inbox_notification(notification, json, token);

        if (notification->unread) {
            unread_notifications++;
        }
    }
}