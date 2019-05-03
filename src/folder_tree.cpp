#include "folder_tree.h"
#include "xxhash.h"
#include "jsmn.h"
#include "id_hash_map.h"
#include "lazy_array.h"
#include "main.h"
#include "platform.h"
#include "ui.h"
#include "renderer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <cctype>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>

/**
 * Folder tree is implemented using 3 arrays and is similar to how it would be stored in a
 * relational database.
 *
 * 1. Data store
 *  Stores actual static folder data (title, color, amount of children)
 *
 * 2. Many-to-many Parent to Child store
 *  Stores pairs of parent folder handle to child folder handle. The pairs are sorted by parent id.
 *  This way we can quickly iterate through all direct children of a folder by binary searching for the top pair
 *  Effectively this is an edge list and a simple-enough way to store a graph, the reason this data structure was picked
 *      is that it allows for easier memory management and updating.
 *
 * 3. Flattened folder tree
 *  Akin to task_list.cpp this is an array representation of a folder graph.
 *  It is built using Data Store + Parent to Child store and is rebuilt whenever a node is expanded/closed or new data
 *      comes in.
 */

struct Folder_Handle {
    s32 value;

    Folder_Handle(){};

    explicit Folder_Handle(s32 v) : value(v) {};

    explicit operator s32() const {
        return value;
    }

    bool operator ==(const Folder_Handle& handle) const {
        return value == handle.value;
    }

    bool operator !=(const Folder_Handle& handle) const {
        return value != handle.value;
    }
};

struct Parent_Child_Pair {
    Folder_Handle parent;
    Folder_Handle child;

    bool is_child_expanded;
};

struct Flattened_Folder_Node {
    u32 nesting;
    bool skeleton;
    Parent_Child_Pair* pair;
    Folder_Handle source; // TODO could be a pointer since we are rebuilding flattened_tree on changes anyway?
};

const Folder_Handle NULL_FOLDER_HANDLE(-1);

static Lazy_Array<Flattened_Folder_Node, 64> flattened_folder_tree{};
static Lazy_Array<Parent_Child_Pair, 64> parent_child_pairs{};
// We can't use Id_Hash_Map<Folder_Id, Folder_Handle, NULL_FOLDER_HANDLE> because C++ reasons
static Id_Hash_Map<Folder_Id, s32, -1> folder_id_to_handle_map{};

static char search_buffer[128];

Folder_Handle root_node = NULL_FOLDER_HANDLE;
Array<Folder_Tree_Node> all_nodes{};

Array<Folder> starred_folders{};
Array<Folder> suggested_folders{};
Array<Folder_Tree_Node*> folder_tree_search_result{};

inline Folder_Tree_Node* get_folder_node_by_handle(Folder_Handle handle) {
    return all_nodes.data + (s32) handle;
}

inline Folder_Handle get_handle_by_folder_id(Folder_Id folder_id, u32 id_hash) {
    return Folder_Handle(id_hash_map_get(&folder_id_to_handle_map, folder_id, id_hash));
}

static Folder_Handle get_or_push_folder_node(Folder_Id folder_id, u32 id_hash) {
    Folder_Handle handle = get_handle_by_folder_id(folder_id, id_hash);

    if (handle != NULL_FOLDER_HANDLE) {
        return handle;
    }

    Folder_Handle new_handle = Folder_Handle(all_nodes.length);
    Folder_Tree_Node* new_node = get_folder_node_by_handle(new_handle);
    new_node->id = folder_id;
    new_node->id_hash = id_hash;
    new_node->num_children = 0;
    new_node->children_loaded = false;

    id_hash_map_put(&folder_id_to_handle_map, (s32) new_handle, new_node->id, new_node->id_hash);

    all_nodes.length++;

    return new_handle;
}

static int compare_parent_child_pairs(const void* a, const void* b) {
    Parent_Child_Pair* pair_a = (Parent_Child_Pair*) a;
    Parent_Child_Pair* pair_b = (Parent_Child_Pair*) b;

    int result = pair_a->parent.value - pair_b->parent.value;

    if (result == 0) {
        // TODO might be slow
        String a_name = get_folder_node_by_handle(pair_a->child)->name;
        String b_name = get_folder_node_by_handle(pair_b->child)->name;

        return strncmp(a_name.start, b_name.start, MIN(a_name.length, b_name.length));
    }

    return result;
}

static int compare_folder_handle_with_parent_child_pair_parent(const void* key, const void* value) {
    Folder_Handle* key_handle = (Folder_Handle*) key;
    Parent_Child_Pair* value_pair = (Parent_Child_Pair*) value;

    return key_handle->value - value_pair->parent.value;
}

// TODO use the num_children param
static Parent_Child_Pair* find_top_parent_child_pair_by_parent_handle(Folder_Handle handle, u32 num_children) {
    Parent_Child_Pair* search_result = (Parent_Child_Pair*) bsearch(
            &handle,
            parent_child_pairs.data,
            parent_child_pairs.length,
            sizeof(Parent_Child_Pair),
            compare_folder_handle_with_parent_child_pair_parent
    );

    if (search_result) {
        // TODO could be made more efficient if we start from MAX(search_result - num_children, parent_child_pairs.data)
        // TODO and traverse forward instead of backwards

        // Searching for the topmost Parent_Child_Pair with the same parent
        Folder_Handle original_parent = search_result->parent;

        // Going backwards
        for (; search_result > parent_child_pairs.data; search_result--) {
            if (original_parent != search_result->parent) {
                search_result++;
                break;
            }
        }

        return search_result;
    }

    return NULL;
}

static void rebuild_flattened_folder_tree_recursively(Folder_Handle parent_handle, u32 nesting) {
    Folder_Tree_Node* parent_node = get_folder_node_by_handle(parent_handle);
    Parent_Child_Pair* top_pair = find_top_parent_child_pair_by_parent_handle(parent_handle, parent_node->num_children);

    for (u32 pair_index = 0; pair_index < parent_node->num_children; pair_index++) {
        Parent_Child_Pair* pair = top_pair + pair_index;
        Flattened_Folder_Node* flattened_node = lazy_array_reserve_n_values(flattened_folder_tree, 1);

        flattened_node->nesting = nesting;
        flattened_node->source = pair->child;
        flattened_node->skeleton = false;
        flattened_node->pair = pair;

        if (pair->is_child_expanded) {
            Folder_Tree_Node* child_node = get_folder_node_by_handle(pair->child);

            if (child_node->children_loaded) {
                rebuild_flattened_folder_tree_recursively(pair->child, nesting + 1);
            } else {
                Flattened_Folder_Node* skeletons = lazy_array_reserve_n_values(flattened_folder_tree, child_node->num_children);

                for (Flattened_Folder_Node* it = skeletons; it != skeletons + child_node->num_children; it++) {
                    it->nesting = nesting + 1;
                    it->skeleton = true;
                }
            }
        }
    }
}

static void rebuild_flattened_folder_tree() {
    qsort(parent_child_pairs.data, parent_child_pairs.length, sizeof(Parent_Child_Pair), compare_parent_child_pairs);

    lazy_array_soft_reset(flattened_folder_tree);

    rebuild_flattened_folder_tree_recursively(root_node, 0);
}

static bool draw_folder_tree_folder_element(ImDrawList* draw_list, ImVec2 element_top_left, ImVec2 element_size, float text_offset, u32 alpha, Folder* folder) {
    const float scale = platform_get_pixel_ratio(); // TODO might be slow
    const float text_height = ImGui::GetFontSize();

    static const u32 hover_color = argb_to_agbr(0x80a3acb6);

    ImVec2 content_top_left = element_top_left + ImVec2(text_offset, 0);
    ImVec2 button_top_left = content_top_left + ImVec2(36.0f * scale, 0.0f);
    ImVec2 button_size{ element_top_left.x + element_size.x - button_top_left.x, element_size.y };
    ImVec2 text_top_left = content_top_left + ImVec2(40.0f * scale, element_size.y / 2.0f - text_height / 2.0f);

    Button_State button_state = button("folder_tree_node", button_top_left, button_size);

    if (button_state.clipped) return button_state.pressed;

    if (button_state.hovered) {
        ImVec2 button_bottom_right = element_top_left + element_size;

        draw_list->AddRectFilled(element_top_left, button_bottom_right, hover_color, 4.0f * scale, ImDrawCornerFlags_Right);
    }

    draw_list->AddRectFilled(element_top_left, element_top_left + ImVec2(scale * 3.0f, element_size.y), folder->color->background);
    draw_list->AddText(text_top_left, 0x00FFFFFF | (alpha << 24), folder->name.start, folder->name.start + folder->name.length);

    return button_state.pressed;
}

static bool folder_tree_node_element(ImDrawList* draw_list, ImVec2 element_top_left, ImVec2 element_size, u32 nesting_level, u32 alpha, Flattened_Folder_Node* flattened_node) {
    float scale = platform_get_pixel_ratio();
    float content_offset = 18.0f * scale * nesting_level;

    ImVec2 arrow_point = element_top_left + ImVec2(26.0f * scale + content_offset, element_size.y / 2.0f);

    Folder_Tree_Node* node = get_folder_node_by_handle(flattened_node->source);

    if (draw_folder_tree_folder_element(draw_list, element_top_left, element_size, content_offset, alpha, node)) {
        select_and_request_folder_by_id(node->id);
    }

    bool& is_expanded = flattened_node->pair->is_child_expanded;
    bool expand_button_was_clicked = false;

    if (node->num_children) {
        expand_button_was_clicked = draw_expand_arrow_button(draw_list, arrow_point, element_size.y, is_expanded);

        if (expand_button_was_clicked) {
            is_expanded = !is_expanded;

            if (is_expanded) {
                request_folder_children_for_folder_tree(node->id);
            }
        }
    }

    return expand_button_was_clicked;
}

// TODO signature similar to folder_tree_node_element is preferred
static void folder_tree_node_skeleton(ImDrawList* draw_list, Vertical_Layout& layout, u32 nesting_level) {
    float width = ImGui::GetContentRegionAvailWidth();
    float element_height = 30.0f * layout.scale; // TODO lots of math duplication
    float scale = platform_get_pixel_ratio(); // TODO lots of math duplication
    float content_offset = 18.0f * scale * nesting_level + 40.0f * scale; // TODO lots of math duplication
    float text_height = ImGui::GetFontSize();

    ImVec2 skeleton_offset { content_offset, element_height / 2.0f - text_height / 2.0f };
    ImVec2 skeleton_top_left = layout.cursor + skeleton_offset;
    ImVec2 skeleton_bottom_right{ layout.cursor.x + width, skeleton_top_left.y + text_height };

    draw_list->AddRectFilled(skeleton_top_left, skeleton_bottom_right, 0x80ffffff, 2.0f * layout.scale);
}

static void draw_flattened_folder_tree(Vertical_Layout& layout) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 element_size{ ImGui::GetContentRegionAvailWidth(), 30.0f * layout.scale };

    bool should_rebuild_flattened_tree = false;

    float frame_offset = (layout.cursor.y - layout.top_left.y);
    float content_height = ImGui::GetWindowHeight();
    float offset_scroll_position = ImGui::GetScrollY() - frame_offset;

    u32 first_visible_item = (u32) MAX(0, (s32) floorf(offset_scroll_position / element_size.y));
    u32 last_visible_item = (u32) MIN(flattened_folder_tree.length, (s32) ceilf((offset_scroll_position + content_height) / element_size.y));
    u32 items_out_of_sight_at_the_bottom = flattened_folder_tree.length - last_visible_item;

    layout_advance(layout, first_visible_item * element_size.y);

    for (Flattened_Folder_Node* it = flattened_folder_tree.data + first_visible_item; it != flattened_folder_tree.data + last_visible_item; it++) {
        ImVec2 element_top_left = layout.cursor;

        ImGui::PushID(it);

        if (it->skeleton) {
            folder_tree_node_skeleton(draw_list, layout, it->nesting);
        } else {
            u32 parent_finished_loading_children_at = get_folder_node_by_handle(it->pair->parent)->finished_loading_children_at;
            u32 alpha = (u32) lroundf(lerp(parent_finished_loading_children_at, tick, 200, 12)) + 55;

            if (folder_tree_node_element(draw_list, element_top_left, element_size, it->nesting, alpha, it)) {
                should_rebuild_flattened_tree = true;
            }
        }

        ImGui::PopID();

        layout_advance(layout, element_size.y);
    }

    layout_advance(layout, items_out_of_sight_at_the_bottom * element_size.y);

    if (should_rebuild_flattened_tree) {
        rebuild_flattened_folder_tree();
    }
}

static void draw_star_icon_filled(ImDrawList* draw_list, ImVec2 center, float scale) {
    ImVec2 tri_right[] = {
            {  0.00f, -0.80f },
            {  0.50f,  0.68f },
            { -0.30f,  0.10f }
    };

    ImVec2 tri_left[] = {
            {  0.00f, -0.80f },
            {  0.30f,  0.10f },
            { -0.50f,  0.68f }
    };

    ImVec2 tri_down[] = {
            { -0.80f, -0.26f },
            {  0.80f, -0.26f },
            {  0.00f,  0.32f }
    };

    auto fill = [=] (ImVec2* v) {
        draw_list->AddTriangleFilled(center + v[0] * scale, center + v[1] * scale, center + v[2] * scale, IM_COL32_WHITE);
    };

    fill(tri_right);
    fill(tri_left);
    fill(tri_down);
}

static void draw_star_icon(ImDrawList* draw_list, ImVec2 center, float scale) {
    const ImVec2 right_side[] = {
            {0, 0.8f},
            {0.18f, 0.26f},
            {0.8f, 0.26f},
            {0.3f, -0.1f},
            {0.5f, -0.68f},
            {0, -0.32f},
    };

    for (u32 index = 1; index < ARRAY_SIZE(right_side); index++) {
        ImVec2 a = right_side[index - 1] * ImVec2(1, -1);;
        ImVec2 b = right_side[index] * ImVec2(1, -1);;

        draw_list->AddLine(center + a * scale, center + b * scale, IM_COL32_WHITE, 2.0f);
    }

    for (s32 index = ARRAY_SIZE(right_side) - 2; index >= 0; index--) {
        ImVec2 a = right_side[index + 1] * ImVec2(-1, -1);
        ImVec2 b = right_side[index] * ImVec2(-1, -1);

        draw_list->AddLine(center + a * scale, center + b * scale, IM_COL32_WHITE, 2.0f);
    }
}

static void draw_folder_tree_search_input() {
    static const u32 bottom_border_active_color = color_link;
    static const u32 bottom_border_hover_color = argb_to_agbr(0x99ffffff);
    static const u32 non_active_color = 0x80ffffff;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, color_background_dark);
    ImGui::PushStyleColor(ImGuiCol_Text, 0xFFFFFFFF);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f) * platform_get_pixel_ratio());

    ImVec2 top_left = ImGui::GetCursorScreenPos();

    ImGui::Dummy({ 26.0f * platform_get_pixel_ratio(), 0 });
    ImGui::SameLine();

    ImVec2 placeholder_text_position = ImGui::GetCursorPos() + ImGui::GetStyle().FramePadding;

    float input_width = ImGui::GetContentRegionAvailWidth() - 24.0f * platform_get_pixel_ratio();

    ImGui::PushItemWidth(input_width);

    if (ImGui::InputText("##tree_search", search_buffer, ARRAY_SIZE(search_buffer))) {
        u64 search_start = platform_get_app_time_precise();

        folder_tree_search(search_buffer, &folder_tree_search_result);

        printf("Took %f to search %i elements by %s\n", platform_get_delta_time_ms(search_start), all_nodes.length, search_buffer);
    }

    ImGui::PopItemWidth();

    ImVec2 input_rect_min = ImGui::GetItemRectMin();
    ImVec2 input_rect_max = ImGui::GetItemRectMax();

    ImVec2 post_input = ImGui::GetCursorPos();

    bool is_active = ImGui::IsItemActive();
    bool is_hovered = ImGui::IsItemHovered();

    if (strlen(search_buffer) == 0) {
        ImGui::SetCursorPos(placeholder_text_position);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(non_active_color), "Filter");
    }

    ImVec2& frame_padding = ImGui::GetStyle().FramePadding;

    u32 bottom_border_color = non_active_color;

    if (is_active) {
        bottom_border_color = bottom_border_active_color;
    } else if (is_hovered) {
        bottom_border_color = bottom_border_hover_color;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddLine(
            ImVec2(input_rect_min.x, input_rect_max.y) + frame_padding,
            input_rect_max + frame_padding,
            bottom_border_color,
            1.0f
    );

    {
        float circle_radius = 5.0f * platform_get_pixel_ratio();
        ImVec2 icon_top_left = top_left + ImVec2(16.0f, 8.0f) * platform_get_pixel_ratio();
        ImVec2 circle_center = icon_top_left + ImVec2(circle_radius, circle_radius);
        ImVec2 handle_offset = ImVec2(1, 1);
        handle_offset *= ImInvLength(handle_offset, 1.0f); // Normal
        handle_offset *= circle_radius;

        draw_list->AddCircle(circle_center, circle_radius, IM_COL32_WHITE, 32, 2.0f);
        draw_list->AddLine(circle_center + handle_offset * 0.98f, circle_center + handle_offset * 2.0f, IM_COL32_WHITE, 3.0f);
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(post_input);

    ImGui::Dummy({ 0, 12.0f * platform_get_pixel_ratio() });
}

static void draw_folder_collection_header(ImDrawList* draw_list, ImVec2 top_left, ImVec2 element_size, const char* text) {
    ImGui::PushFont(font_19px_bold);

    float font_height = ImGui::GetFontSize();

    ImVec2 text_top_left = top_left + ImVec2(40.0f * platform_get_pixel_ratio(), element_size.y / 2.0f - font_height / 2.0f);

    draw_list->AddText(text_top_left, IM_COL32_WHITE, text, text + strlen(text));

    ImGui::PopFont();
}

static void draw_starred_folders(Vertical_Layout& layout) {
    float scale = platform_get_pixel_ratio();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 element_size{ ImGui::GetContentRegionAvailWidth(), 30.0f * scale };

    draw_star_icon_filled(draw_list, layout.cursor + ImVec2(23.0f, 16.0f) * scale, scale * 10.0f);
    draw_folder_collection_header(draw_list, layout.cursor, element_size, "Starred");

    layout_advance(layout, element_size.y);

    for (Folder* it = starred_folders.data; it != starred_folders.data + starred_folders.length; it++) {
        ImGui::PushID(it);

        if (draw_folder_tree_folder_element(draw_list, layout.cursor, element_size, 0, 0xff, it)) {
            select_and_request_folder_by_id(it->id);
        }

        ImGui::PopID();

        layout_advance(layout, element_size.y);
    }

    {
        layout_advance(layout, 6.0f * scale);

        const u32 separator_color = argb_to_agbr(0xff344b5d);

        float separator_x_offset = 26.0f * scale;

        ImVec2 separator_left = ImVec2(layout.cursor.x + separator_x_offset, layout.cursor.y);
        ImVec2 separator_right = separator_left + ImVec2(element_size.x - separator_x_offset, 0);

        draw_list->AddLine(separator_left, separator_right, separator_color);

        layout_advance(layout, 6.0f * scale);
    }
}

void draw_folder_tree(float column_width) {
    ImGuiID folder_tree_id = ImGui::GetID("folder_tree");

    ImGui::PushStyleColor(ImGuiCol_FrameBg, color_background_dark);

    ImGui::BeginChildFrame(folder_tree_id, ImVec2(column_width, -1));
    draw_folder_tree_search_input();

    ImGui::PushStyleColor(ImGuiCol_Text, 0xFFFFFFFF);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, color_background_dark);
    ImGui::PushFont(font_19px);
    ImGui::ListBoxHeader("##folder_tree_content", ImVec2(-1, -1));

    Vertical_Layout layout = vertical_layout(ImGui::GetCursorScreenPos());

    u32 buffer_length = strlen(search_buffer);
    if (buffer_length > 0 && folder_tree_search_result.data) {
        for (Folder_Tree_Node** node_pointer = folder_tree_search_result.data; node_pointer != folder_tree_search_result.data + folder_tree_search_result.length; node_pointer++) {
            Folder_Tree_Node* node = *node_pointer;
            char* name = string_to_temporary_null_terminated_string(node->name);

            ImGui::PushID(node->id);
            if (ImGui::Selectable(name)) {
                select_and_request_folder_by_id(node->id);
            }
            ImGui::PopID();
        }
    } else {
        if (starred_folders.length > 0) {
            draw_starred_folders(layout);
        }

        if (root_node != NULL_FOLDER_HANDLE) {
            if (!get_folder_node_by_handle(root_node)->children_loaded) {
                draw_window_loading_indicator();
            }

            ImVec2 element_size{ ImGui::GetContentRegionAvailWidth(), 30.0f * layout.scale };

            ImGui::GetWindowDrawList()->AddCircleFilled(layout.cursor + ImVec2(23.0f, 16.0f) * layout.scale, 3.0f * layout.scale, IM_COL32_WHITE, 24);

            draw_folder_collection_header(ImGui::GetWindowDrawList(), layout.cursor, element_size, "Shared with me");

            layout_advance(layout, element_size.y);

            draw_flattened_folder_tree(layout);

            layout_push_item_size(layout);
        }
    }

    if (ImGui::GetScrollY() > 0.01f) {
        draw_scroll_shadow(ImGui::GetWindowDrawList(), layout.top_left, column_width, layout.scale);
    }

    ImGui::ListBoxFooter();
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();

    ImGui::EndChildFrame();
    ImGui::PopStyleColor();
}

void folder_tree_init(Folder_Id root_node_id) {
    id_hash_map_init(&folder_id_to_handle_map);

    all_nodes.data = (Folder_Tree_Node*) REALLOC(all_nodes.data, sizeof(Folder_Tree_Node));

    static Folder_Color root_node_color(0, 0xff555555, 0);

    root_node = get_or_push_folder_node(root_node_id, hash_id(root_node_id));

    Folder_Tree_Node* root = get_folder_node_by_handle(root_node);
    root->color = &root_node_color;
    root->name.start = (char*) "Root"; // TODO use the string storage
    root->name.length = strlen(root->name.start);
}

Folder_Tree_Node* find_folder_tree_node_by_id(Folder_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    Folder_Handle handle = get_handle_by_folder_id(id, id_hash);

    if (handle == NULL_FOLDER_HANDLE) {
        return NULL;
    }

    return &all_nodes[(s32) handle];
}

static void try_add_parent_child_pair(Folder_Handle parent, Folder_Handle child) {
    // TODO the slowest of them all! We could binary search the data or use a hash map
    for (Parent_Child_Pair* it = parent_child_pairs.data; it != parent_child_pairs.data + parent_child_pairs.length; it++) {
        if (it->parent == parent && it->child == child) {
            return;
        }
    }

    Parent_Child_Pair* pair = lazy_array_reserve_n_values(parent_child_pairs, 1);
    pair->parent = parent;
    pair->child = child;
    pair->is_child_expanded = false;
}

Folder_Color* string_to_folder_color(String string);

static void process_folder_tree_child_object(Folder_Handle parent_handle, char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    u32 num_children = 0;

    Folder_Tree_Node folder_data;
    folder_data.name.start = NULL;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* value_token = token;

        // TODO string comparison there is inefficient, can be faster
        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, value_token, folder_data.name);
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, value_token, folder_data.id);
        } else if (json_string_equals(json, property_token, "color")) {
            String color;

            json_token_to_string(json, value_token, color);

            folder_data.color = string_to_folder_color(color);
        } else if (json_string_equals(json, property_token, "childIds")) {
            assert(value_token->type == JSMN_ARRAY);

            num_children = value_token->size;

            eat_json(token);
            token--;
        } else {
            eat_json(token);
            token--;
        }
    }

    Folder_Handle new_handle = get_or_push_folder_node(folder_data.id, hash_id(folder_data.id));
    Folder_Tree_Node* new_node = get_folder_node_by_handle(new_handle);

    new_node->name = folder_data.name;
    new_node->color = folder_data.color;
    new_node->num_children = num_children;
    new_node->finished_loading_children_at = tick;

    if (parent_handle != NULL_FOLDER_HANDLE) {
        try_add_parent_child_pair(parent_handle, new_handle);
    }
}

void folder_tree_search(const char* query, Array<Folder_Tree_Node*>* result) {

}

void process_multiple_folders_data(char* json, u32 data_size, jsmntok_t*& token) {
    u32 new_size = all_nodes.length + data_size;

    if (all_nodes.length < new_size) {
        all_nodes.data = (Folder_Tree_Node*) REALLOC(all_nodes.data, sizeof(Folder_Tree_Node) * new_size);
    }

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_tree_child_object(NULL_FOLDER_HANDLE, json, token);
    }
}

static void process_plain_folder_data_object(Folder* folder, char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, next_token, folder->name);
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, folder->id);
        } else if (json_string_equals(json, property_token, "color")) {
            String color;

            json_token_to_string(json, next_token, color);

            folder->color = string_to_folder_color(color);
        } else {
            eat_json(token);
            token--;
        }
    }
}

void process_folder_tree_children_request(Folder_Id parent_id, char* json, jsmntok_t* tokens, u32 num_tokens) {
    Folder_Handle parent_handle = get_handle_by_folder_id(parent_id, hash_id(parent_id));

    if (parent_handle == NULL_FOLDER_HANDLE) {
        assert(!"Parent node not found");
        return;
    }

    // TODO ugly copypaste from process_json_data_segment, ugh!
    for (jsmntok_t* token = tokens; token < tokens + num_tokens; token++) {
        if (json_string_equals(json, token, "data")) {
            jsmntok_t* next_token = ++token;

            assert(next_token->type == JSMN_ARRAY);

            token++;

            u32 new_size = all_nodes.length + next_token->size;

            if (all_nodes.length < new_size) {
                all_nodes.data = (Folder_Tree_Node*) REALLOC(all_nodes.data, sizeof(Folder_Tree_Node) * new_size);
            }

            for (u32 array_index = 0; array_index < next_token->size; array_index++) {
                process_folder_tree_child_object(parent_handle, json, token);
            }

            Folder_Tree_Node* parent_node = get_folder_node_by_handle(parent_handle);

            parent_node->children_loaded = true;
            parent_node->finished_loading_children_at = tick;
            parent_node->num_children = (u32) next_token->size;
        }
    }

    rebuild_flattened_folder_tree();
}

void process_suggested_folders_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (suggested_folders.length < data_size) {
        suggested_folders.data = (Folder*) REALLOC(suggested_folders.data, sizeof(Folder) * data_size);
    }

    suggested_folders.length = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        Folder* suggested_folder = &suggested_folders[suggested_folders.length++];

        process_plain_folder_data_object(suggested_folder, json, token);
    }
}

void process_starred_folders_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (starred_folders.length < data_size) {
        starred_folders.data = (Folder*) REALLOC(starred_folders.data, sizeof(Folder) * data_size);
    }

    starred_folders.length = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        Folder* starred_folder = &starred_folders[starred_folders.length++];

        process_plain_folder_data_object(starred_folder, json, token);
    }
}

Folder_Color* string_to_folder_color(String string) {
    static Folder_Color None(0, 0xff555555, 0);
    static Folder_Color Purple1(0xFFE1BEE7, 0xff8e24aa, 0xffeecbf4);
    static Folder_Color Purple2(0xFFCE93D8, 0xff8e24aa, 0xffe4a8ee);
    static Folder_Color Purple3(0xFFBA68C8, 0xfff3e5f5, 0xffcb75da);
    static Folder_Color Purple4(0xFF8E24AA, 0xfff3e5f5, 0xffa637c5);
    static Folder_Color Indigo1(0xFFD1C4E9, 0xff5e35b1, 0xffddd0f5);
    static Folder_Color Indigo2(0xFFB39DDB, 0xff5e35b1, 0xffc7b0ef);
    static Folder_Color Indigo3(0xFF9575CD, 0xffede7f6, 0xffa281dd);
    static Folder_Color Indigo4(0xFF5E35B1, 0xffede7f6, 0xff7146ca);
    static Folder_Color DarkBlue1(0xFFC5CAE9, 0xff3949ab, 0xffd0d6f5);
    static Folder_Color DarkBlue2(0xFF9FA8DA, 0xff3949ab, 0xffb3bbed);
    static Folder_Color DarkBlue3(0xFF7986CB, 0xffe8eaf6, 0xff8492db);
    static Folder_Color DarkBlue4(0xFF3949AB, 0xffe8eaf6, 0xff4b5bc3);
    static Folder_Color Blue1(0xFFBBDEFB, 0xff1976d2, 0xffcce8ff);
    static Folder_Color Blue2(0xFF90CAF9, 0xff1976d2, 0xffaddaff);
    static Folder_Color Blue3(0xFF64B5F6, 0xffe3f2fd, 0xff73c1ff);
    static Folder_Color Blue4(0xFF1E88E5, 0xffe3f2fd, 0xff2b9bfd);
    static Folder_Color Turquoise1(0xFFB2EBF2, 0xff0097a7, 0xffc0f8ff);
    static Folder_Color Turquoise2(0xFF80DEEA, 0xff5e35b1, 0xff97f3ff);
    static Folder_Color Turquoise3(0xFF4DD0E1, 0xffe0f7fa, 0xff59e2f4);
    static Folder_Color Turquoise4(0xFF00ACC1, 0xffe0f7fa, 0xff12c7de);
    static Folder_Color DarkCyan1(0xFFB2DFDB, 0xff00796b, 0xffc2efeb);
    static Folder_Color DarkCyan2(0xFF80CBC4, 0xff00796b, 0xff9be5df);
    static Folder_Color DarkCyan3(0xFF4DB6AC, 0xffe0f2f1, 0xff5dccc0);
    static Folder_Color DarkCyan4(0xFF00897B, 0xffe0f2f1, 0xff18a99a);
    static Folder_Color Green1(0xFFC8E6C9, 0xff388e3c, 0xffd3f1d4);
    static Folder_Color Green2(0xFFA5D6A7, 0xff388e3c, 0xffb9e9ba);
    static Folder_Color Green3(0xFF81C784, 0xffe8f5e9, 0xff8dd690);
    static Folder_Color Green4(0xFF43A047, 0xffe8f5e9, 0xff55b759);
    static Folder_Color YellowGreen1(0xFFE6EE9C, 0xff9e9d24, 0xfff6ffae);
    static Folder_Color YellowGreen2(0xFFDCE775, 0xff9e9d24, 0xfff3ff8d);
    static Folder_Color YellowGreen3(0xFFC0CA33, 0xfff9fbe7, 0xffd5e141);
    static Folder_Color YellowGreen4(0xFFAFB42B, 0xfff9fbe7, 0xffc7cd3d);
    static Folder_Color Yellow1(0xFFFFF59D, 0xfff57f17, 0xfffff8ba);
    static Folder_Color Yellow2(0xFFFFEE58, 0xfff57f17, 0xfffff38a);
    static Folder_Color Yellow3(0xFFFBC02D, 0xfffffde7, 0xffffca49);
    static Folder_Color Yellow4(0xFFF9A825, 0xfffffde7, 0xffffb640);
    static Folder_Color Orange1(0xFFFFCC80, 0xffe65100, 0xffffdba6);
    static Folder_Color Orange2(0xFFFFB74D, 0xffe65100, 0xffffcc82);
    static Folder_Color Orange3(0xFFFF9800, 0xfffff3e0, 0xffffa726);
    static Folder_Color Orange4(0xFFF57C00, 0xfffff3e0, 0xffff901d);
    static Folder_Color Red1(0xFFFFCDD2, 0xffd32f2f, 0xffffdcdf);
    static Folder_Color Red2(0xFFEF9A9A, 0xffd32f2f, 0xffffadad);
    static Folder_Color Red3(0xFFE57373, 0xffffebee, 0xfff47c7c);
    static Folder_Color Red4(0xFFE53935, 0xffffebee, 0xfffb4641);
    static Folder_Color Pink1(0xFFF8BBD0, 0xffc2185b, 0xffffcadc);
    static Folder_Color Pink2(0xFFF48FB1, 0xffc2185b, 0xffffa8c5);
    static Folder_Color Pink3(0xFFF06292, 0xfffce4ec, 0xffff6c9d);
    static Folder_Color Pink4(0xFFD81B60, 0xfffce4ec, 0xfff12972);
    static Folder_Color Gray1(0xFFB0BEC5, 0xff2d3e4f, 0xffc5d2d9);
    static Folder_Color Gray2(0xFF546E7A, 0xffeceff1, 0xff698692);
    static Folder_Color Gray3(0xFF2D3E4F, 0xffeceff1, 0xff485b6c);

    static Folder_Color* blue[] = {
            &Blue1,
            &Blue2,
            &Blue3,
            &Blue4
    };

    static Folder_Color* dark_blue[] = {
            &DarkBlue1,
            &DarkBlue2,
            &DarkBlue3,
            &DarkBlue4
    };

    static Folder_Color* dark_cyan[] = {
            &DarkCyan1,
            &DarkCyan2,
            &DarkCyan3,
            &DarkCyan4
    };

    static Folder_Color* gray[] = {
            &Gray1,
            &Gray2,
            &Gray3
    };

    static Folder_Color* green[] = {
            &Green1,
            &Green2,
            &Green3,
            &Green4
    };

    static Folder_Color* indigo[] = {
            &Indigo1,
            &Indigo2,
            &Indigo3,
            &Indigo4
    };

    static Folder_Color* orange[] = {
            &Orange1,
            &Orange2,
            &Orange3,
            &Orange4
    };

    static Folder_Color* pink[] = {
            &Pink1,
            &Pink2,
            &Pink3,
            &Pink4
    };

    static Folder_Color* purple[] = {
            &Purple1,
            &Purple2,
            &Purple3,
            &Purple4
    };

    static Folder_Color* red[] = {
            &Red1,
            &Red2,
            &Red3,
            &Red4,
    };

    static Folder_Color* turquoise[] = {
            &Turquoise1,
            &Turquoise2,
            &Turquoise3,
            &Turquoise4,
    };

    static Folder_Color* yellow[] = {
            &Yellow1,
            &Yellow2,
            &Yellow3,
            &Yellow4,
    };

    static Folder_Color* yellow_green[] = {
            &YellowGreen1,
            &YellowGreen2,
            &YellowGreen3,
            &YellowGreen4,
    };

    char s = *string.start;

#define char_at_to_index(at) *(string.start + (at)) - '1'

    switch (s) {
        case 'N'/*one*/: return &None;

        case 'B'/*lue*/: return blue[char_at_to_index(4)];
        case 'D'/*ark*/: {
            switch (*(string.start + 4)) {
                case 'B'/*lue*/: return dark_blue[char_at_to_index(8)];
                case 'C'/*yan*/: return dark_cyan[char_at_to_index(8)];
            }

            break;
        }

        case 'G'/*ray or Green*/: {
            switch (string.length) {
                case 5: return gray[char_at_to_index(4)];
                case 6: return green[char_at_to_index(5)];
            }
        }

        case 'I'/*ndigo*/: return indigo[char_at_to_index(6)];
        case 'O'/*range*/: return orange[char_at_to_index(6)];

        case 'P'/*ink or Purple or Person*/: {
            switch (string.length) {
                case 5: return pink[char_at_to_index(4)];
                case 6: return &None;
                case 7: return purple[char_at_to_index(6)];
            }
        }

        case 'R'/*ed*/: return red[char_at_to_index(3)];
        case 'T'/*urquoise*/: return turquoise[char_at_to_index(9)];

        case 'Y'/*ellow or YellowGreen*/: {
            switch (string.length) {
                case 7: return yellow[char_at_to_index(6)];
                case 12: return yellow_green[char_at_to_index(11)];
            }
        }
    }

    printf("Unrecognized color: %.*s\n", string.length, string.start);

    return &None;

#undef char_at_to_index
}