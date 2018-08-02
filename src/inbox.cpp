#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>
#include "common.h"
#include "json.h"
#include "users.h"
#include "workflows.h"
#include "ui.h"
#include "renderer.h"
#include "platform.h"

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
    Task_Id task;
    String task_title;
    bool unread;

    union {
        Comment_Notification comment;
        Status_Notification status;
    };
};

static u32 unread_notifications = 0;
static Lazy_Array<Inbox_Notification, 8> notifications{};

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
        } else if (json_string_equals(json, property_token, "taskId")) {
            json_token_to_right_part_of_id16(json, next_token, notification->task);
        } else if (json_string_equals(json, property_token, "taskTitle")) {
            json_token_to_string(json, next_token, notification->task_title);
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

static void draw_notification_specific_data(ImDrawList* draw_list, ImVec2 top_left, Inbox_Notification* notification) {
    switch (notification->type) {
        case Inbox_Notification_Type_Assign: {
            const char* text_start = "Assigned to you";
            const char* text_end = text_start + strlen(text_start);

            draw_list->AddText(top_left, color_black_text_on_white, text_start, text_end);

            break;
        }

        case Inbox_Notification_Type_Mention: {
            String comment_text = notification->comment.text;

            char* text_start, * text_end;
            tprintf("Mentioned you: %.*s", &text_start, &text_end, comment_text.length, comment_text.start);

            draw_list->AddText(top_left, color_black_text_on_white, text_start, text_end);

            break;
        }

        case Inbox_Notification_Type_Status: {
            Custom_Status* old_status = find_custom_status_by_id(notification->status.old_status);
            Custom_Status* new_status = find_custom_status_by_id(notification->status.new_status);

            if (!old_status || !new_status) {
                // TODO @DataIntegrity should work
                return;
            }

            Horizontal_Layout layout = horizontal_layout(top_left, ImGui::GetTextLineHeight());

            auto draw_status_title = [&layout, draw_list](Custom_Status* status) {
                char* text_start = status->name.start;
                char* text_end = status->name.start + status->name.length;

                ImVec2 text_size = ImGui::CalcTextSize(text_start, text_end);

                draw_list->AddText(layout.cursor, status->color, text_start, text_end);

                layout_advance(layout, text_size.x);
            };

            draw_status_title(old_status);

            layout_advance(layout, 16.0f * layout.scale);
            draw_expand_arrow_button(draw_list, layout.cursor + ImVec2(0, layout.row_height / 2.0f), layout.row_height, false);
            layout_advance(layout, 16.0f * layout.scale);

            draw_status_title(new_status);

            break;
        }
    }
}

static void draw_inbox_header() {
    ImVec2 top_left = ImGui::GetCursorScreenPos();

    float scale = platform_get_pixel_ratio();
    float header_height = 56.0f * scale;

    ImGui::PushFont(font_28px);

    ImVec2 header_padding = ImVec2(16.0f * scale, header_height / 2.0f - ImGui::GetFontSize() / 2.0f);

    const char* text_start = "Inbox";
    const char* text_end = text_start + strlen(text_start);

    ImGui::GetWindowDrawList()->AddText(top_left + header_padding, color_black_text_on_white, text_start, text_end);
    ImGui::PopFont();
    ImGui::Dummy({ 0, header_height });
}

static bool draw_inbox_entry(ImDrawList* draw_list, ImVec2 top_left, ImVec2 item_size, Inbox_Notification* notification, User* author, bool draw_top_border) {
    Horizontal_Layout layout = horizontal_layout(top_left, item_size.y);

    float horizontal_padding = 16.0f * layout.scale;
    float avatar_side_px = 32.0f * layout.scale;
    float avatar_margin_right = 16.0f * layout.scale;

    Button_State state = button(notification, layout.cursor, item_size);

    if (state.clipped) {
        return state.pressed;
    }

    String task_title = notification->task_title;

    u32 background_color = 0;

    if (notification->unread) {
        background_color = state.hovered ? argb_to_agbr(0xffe5f6ff) : argb_to_agbr(0xffedf9ff);
    } else if (state.hovered) {
        background_color = 0xfff5f5f5;
    }

    if (background_color) {
        draw_list->AddRectFilled(layout.cursor, layout.cursor + item_size, background_color);
    }

    if (draw_top_border) {
        draw_list->AddLine(layout.cursor, layout.cursor + ImVec2(item_size.x, 0), 0xffebebeb);
    }

    layout_advance(layout, horizontal_padding);
    draw_circular_user_avatar(draw_list, author, layout_center_vertically(layout, avatar_side_px), avatar_side_px);

    layout_advance(layout, avatar_side_px);
    layout_advance(layout, avatar_margin_right);

    float two_lines_of_text = ImGui::GetTextLineHeight() * 2.0f;

    ImVec2 text_block_top_left = layout_center_vertically(layout, two_lines_of_text);
    ImVec2 second_line_top_left = text_block_top_left + ImVec2(0, ImGui::GetTextLineHeight());

    draw_list->AddText(text_block_top_left, 0x8f000000, task_title.start, task_title.start + task_title.length);
    draw_notification_specific_data(draw_list, second_line_top_left, notification);

    return state.pressed;
}

static Inbox_Notification* find_notification_by_id(Inbox_Notification_Id id) {
    // We'll just hope there isn't ever that much data for this to matter
    for (Inbox_Notification* it = notifications.data; it != notifications.data + notifications.length; it++) {
        if (it->id == id) {
            return it;
        }
    }

    return NULL;
}

void draw_inbox() {
    ImGui::BeginChildFrame(ImGui::GetID("inbox"), ImVec2(-1, -1));

    draw_inbox_header();

    ImGui::BeginChildFrame(ImGui::GetID("inbox_content"), ImVec2(-1, -1));

    Vertical_Layout layout = vertical_layout(ImGui::GetCursorScreenPos());
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float vertical_padding = 8.0f * layout.scale;
    float content_height = 38.0f * layout.scale;
    float content_width = ImGui::GetContentRegionAvailWidth();
    float element_height = vertical_padding + content_height + vertical_padding;

    ImGui::PushFont(font_19px);

    for (Inbox_Notification* it = notifications.data; it != notifications.data + notifications.length; it++) {
        User* author = find_user_by_id(it->author);

        if (!author) {
            // TODO @DataIntegrity should work
            continue;
        }

        if (draw_inbox_entry(draw_list, layout.cursor, { content_width, element_height }, it, author, it > notifications.data)) {
            request_task_by_task_id(it->task);
            mark_notification_as_read(it->id);
        }

        layout_advance(layout, element_height);
    }

    if (ImGui::GetScrollY() > 0.01f) {
        draw_scroll_shadow(draw_list, layout.top_left, content_width, layout.scale);
    }

    ImGui::PopFont();

    layout_push_item_size(layout);

    ImGui::EndChildFrame();
    ImGui::EndChildFrame();
}

void process_inbox_data(char* json, u32 data_size, jsmntok_t*& token) {
    for (u32 array_index = 0; array_index < data_size; array_index++) {
        Inbox_Notification new_notification{};
        process_inbox_notification(&new_notification, json, token);

        Inbox_Notification* old_notification = find_notification_by_id(new_notification.id);

        if (old_notification) {
            *old_notification = new_notification;
        } else {
            Inbox_Notification* allocated_notification = lazy_array_reserve_n_values(notifications, 1);

            *allocated_notification = new_notification;
        }
    }

    unread_notifications = 0;

    for (Inbox_Notification* it = notifications.data; it != notifications.data + notifications.length; it++) {
        if (it->unread) {
            unread_notifications++;
        }
    }
}