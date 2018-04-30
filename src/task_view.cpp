#include "task_view.h"
#include "common.h"
#include "main.h"
#include "funimgui.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "platform.h"

#include <imgui.h>
#include <cstdlib>
#include <html_entities.h>

void draw_task_contents() {
    float wrap_width = ImGui::GetColumnWidth(-1) - 30.0f; // Accommodate for scroll bar

    ImGuiID task_content_id = ImGui::GetID("task_content");
    ImGui::BeginChildFrame(task_content_id, ImVec2(-1, -1), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float alpha = lerp(finished_loading_task_at, tick, 1.0f, 8);

    ImVec4 title_color = ImVec4(0, 0, 0, alpha);

    float wrap_pos = ImGui::GetCursorPosX() + wrap_width;
    ImGui::PushTextWrapPos(wrap_pos);
    ImGui::PushFont(font_header);
    ImGui::TextColored(title_color, "%.*s", current_task.title.length, current_task.title.start);
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    for (u32 index = 0; index < current_task.num_assignees; index++) {
        bool is_last = index == (current_task.num_assignees - 1);

        // TODO temporary code, use a hash map!
        for (u32 user_index = 0; user_index < users_count; user_index++) {
            User* user = &users[user_index];

            if (are_ids_equal(&user->id, &current_task.assignees[index])) {
                const char* pattern_last = "%.*s %.*s";
                const char* pattern_preceding = "%.*s %.*s,";

                ImGui::TextColored(title_color, is_last ? pattern_last : pattern_preceding,
                                   user->firstName.length, user->firstName.start,
                                   user->lastName.length, user->lastName.start
                );

                if (!is_last) {
                    ImGui::SameLine();
                }

                break;
            }
        }
    }

    if (ImGui::Button("Open in Wrike")) {
        platform_open_in_wrike(current_task.permalink);
    }

    ImGui::Separator();

    ImGui::BeginChild("task_description_and_comments", ImVec2(-1, -1));
    if (current_task.description_strings > 0) {
        Rich_Text_String* text_start = current_task.description;
        Rich_Text_String* text_end = text_start + current_task.description_strings - 1;

        for (Rich_Text_String* paragraph_end = text_start, *paragraph_start = paragraph_end; paragraph_end <= text_end; paragraph_end++) {
            bool is_newline = paragraph_end->text.length == 0;
            bool should_flush_text = is_newline || paragraph_end == text_end;

            char* string_start = paragraph_start->text.start;
            char* string_end = paragraph_end->text.start + paragraph_end->text.length;

            if (is_newline && (string_end - string_start) == 0) {
                ImGui::NewLine();
                paragraph_start = paragraph_end + 1;
                continue;
            }

            if (should_flush_text && paragraph_end - paragraph_start) {
#if 0
                const char* check_mark = "\u2713";
                const char* check_box = "\u25A1";

                if (paragraph_start->text.length >= 4) {
                    bool is_check_mark = strncmp(check_mark, paragraph_start->text.start + 1, strlen(check_mark)) == 0;
                    bool is_check_box = strncmp(check_box, paragraph_start->text.start + 1, strlen(check_box)) == 0;

                    if (is_check_mark || is_check_box) {
                        ImGui::Checkbox("", &is_check_mark);
                        ImGui::SameLine();
                    }
                }
#endif

                float indent = paragraph_start->style.list_depth * 15.0f;

                if (indent) {
                    ImGui::Indent(indent);
                    ImGui::Bullet();
                    ImGui::SameLine();
                }

                add_rich_text(
                        ImGui::GetWindowDrawList(),
                        ImGui::GetCursorScreenPos(),
                        paragraph_start,
                        paragraph_end,
                        wrap_width,
                        alpha
                );

                if (indent) {
                    ImGui::Unindent(indent);
                }

                paragraph_start = paragraph_end + 1;
            }
        }
    }

    ImGui::EndChild();

    ImGui::EndChildFrame();
}

void process_task_data(char* json, u32 data_size, jsmntok_t*& token) {
    // We only request singular tasks now
    assert(data_size == 1);
    assert(token->type == JSMN_OBJECT);

    jsmntok_t* object_token = token++;

    String description{};

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

#define IS_PROPERTY(s) json_string_equals(json, property_token, (s))
#define TOKEN_TO_STRING(s) json_token_to_string(json, next_token, (s))

        if (IS_PROPERTY("title")) {
            TOKEN_TO_STRING(current_task.title);
        } else if (IS_PROPERTY("description")) {
            TOKEN_TO_STRING(description);
        } else if (IS_PROPERTY("permalink")) {
            TOKEN_TO_STRING(current_task.permalink);
        } else if (IS_PROPERTY("responsibleIds")) {
            assert(next_token->type == JSMN_ARRAY);

            if (current_task.num_assignees < next_token->size) {
                current_task.assignees = (Id8*) realloc(current_task.assignees, sizeof(Id8) * next_token->size);
            }

            current_task.num_assignees = 0;

            for (u32 array_index = 0; array_index < next_token->size; array_index++) {
                jsmntok_t* id_token = ++token;

                assert(id_token->type == JSMN_STRING);

                json_token_to_id(json, id_token, current_task.assignees[current_task.num_assignees++]);
            }
        } else {
            eat_json(token);
            token--;
        }
    }

#undef IS_PROPERTY
#undef TOKEN_TO_STRING

    if (current_task.description) {
        free(current_task.description);
    }

    current_task.description_strings = 0;

    parse_rich_text(description, current_task.description, current_task.description_strings);

    u32 total_text_length = 0;

    for (u32 index = 0; index < current_task.description_strings; index++) {
        total_text_length += current_task.description[index].text.length;
    }

    String& description_text = current_task.description_text;
    description_text.start = (char*) realloc(description_text.start, total_text_length);
    description_text.length = total_text_length;

    char* current_location = description_text.start;

    for (u32 index = 0; index < current_task.description_strings; index++) {
        Rich_Text_String& rich_text_string = current_task.description[index];
        String& text = rich_text_string.text;

        if (rich_text_string.text.length > 0) {
            size_t new_length = decode_html_entities_utf8(current_location, text.start, text.start + text.length);

            text.start = current_location;
            text.length = new_length;

            current_location += new_length;
        } else {
            text.start = current_location;
        }
    }

    printf("Parsed description with a total length of %lu\n", total_text_length);
}