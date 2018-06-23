#include "folder_tree.h"
#include "xxhash.h"
#include "jsmn.h"
#include "id_hash_map.h"
#include "lazy_array.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <cctype>

struct Parent_Child_Pair {
    Folder_Tree_Node* parent;

    Folder_Id child_id;
    u32 child_hash;
};

static Lazy_Array<Folder_Tree_Node*, 64> child_nodes{};
static Lazy_Array<Parent_Child_Pair, 64> parent_child_pairs{};
static Id_Hash_Map<Folder_Id, Folder_Tree_Node*> folder_id_to_node_map{};

static u32 current_node = 0;
static u32 total_names_length = 0;
static char* search_index = NULL;

static char* json_content = NULL;

Folder_Tree_Node* root_node = NULL;
Folder_Tree_Node* all_nodes;
Folder_Tree_Node** starred_nodes = NULL;
u32 total_nodes;
u32 total_starred = 0;

List<Suggested_Folder> suggested_folders{};

void folder_tree_init() {
    id_hash_map_init(&folder_id_to_node_map);
}

Folder_Tree_Node* find_folder_tree_node_by_id(Folder_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return id_hash_map_get(&folder_id_to_node_map, id, id_hash);
}

static void add_parent_child_pair(Folder_Tree_Node* parent, Folder_Id child_id) {
    Parent_Child_Pair* pair = lazy_array_reserve_n_values(parent_child_pairs, 1);
    pair->parent = parent;
    pair->child_id = child_id;
    pair->child_hash = hash_id(child_id);
}

Folder_Color* string_to_folder_color(String string);

static void process_folder_tree_data_object(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    String scope;
    u32 num_children = 0;
    bool is_starred = false;

    Folder_Tree_Node* new_node = &all_nodes[current_node++];
    new_node->name.start = NULL;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* value_token = token;

        // TODO string comparison there is inefficient, can be faster
        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, value_token, new_node->name);

            total_names_length += new_node->name.length + 1;

            if (new_node->name.length >= 256) {
                total_names_length++; // 0-terminator
            }
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, value_token, new_node->id);
        } else if (json_string_equals(json, property_token, "scope")) {
            json_token_to_string(json, value_token, scope);
        } else if (json_string_equals(json, property_token, "color")) {
            String color;

            json_token_to_string(json, value_token, color);

            new_node->color = string_to_folder_color(color);
        } else if (json_string_equals(json, property_token, "starred")) {
            assert(value_token->type == JSMN_PRIMITIVE);
            is_starred = *(json + value_token->start) == 't';
        } else if (json_string_equals(json, property_token, "childIds")) {
            assert(value_token->type == JSMN_ARRAY);

            num_children = value_token->size;

            for (u32 array_index = 0; array_index < value_token->size; array_index++) {
                jsmntok_t* id_token = ++token;

                assert(id_token->type == JSMN_STRING);

                Folder_Id child_id;

                json_token_to_right_part_of_id16(json, id_token, child_id);

                // TODO could be adding multiple pairs at once
                add_parent_child_pair(new_node, child_id);
            }
        } else {
            eat_json(token);
            token--;
        }
    }

    u32 id_hash = hash_id(new_node->id);

    new_node->id_hash = id_hash;
    new_node->num_children = 0;
    new_node->is_starred = is_starred;

    if (is_starred) {
        total_starred++;
    }

    if (num_children > 0) {
        new_node->children = lazy_array_reserve_n_values_relative_pointer(child_nodes, num_children);
    }

    id_hash_map_put(&folder_id_to_node_map, new_node, new_node->id, id_hash);

    bool is_root = strlen("WsRoot") == scope.length && strncmp(scope.start, "WsRoot", scope.length) == 0;

    if (is_root) {
        root_node = new_node;
    }
}

void folder_tree_search(const char* query, List<Folder_Tree_Node*>* result) {
    u32 query_length = (u32) strlen(query);

    if (!query_length) {
        return;
    }

    if (!total_nodes) {
        return;
    }

    char* query_lowercase = (char*) talloc(query_length + 1);

    for (u32 index = 0; index < query_length; index++) {
        query_lowercase[index] = (char) tolower(query[index]);
    }

    query_lowercase[query_length] = 0;

    if (!result->data) {
        result->data = (Folder_Tree_Node**) MALLOC(sizeof(Folder_Tree_Node*) * total_nodes);
    }

    result->length = 0;

    char* it = search_index;

    for (u32 node_index = 0; node_index < total_nodes; node_index++) {
        u32 length_or_zero = (u8) *it;

        it++;

        bool has_to_calculate_length = !length_or_zero;

        if (has_to_calculate_length) {
            length_or_zero = (u32) strlen(it);
        }

        if (string_in_substring(it, query_lowercase, length_or_zero)) {
            result->data[result->length++] = &all_nodes[node_index];
        }

        it += length_or_zero;

        if (has_to_calculate_length) {
            it++;
        }
    }
}

static void match_tree_parent_child_pairs() {
    printf("Total pairs: %i\n", parent_child_pairs.length);

    u32 found_pairs = 0;

    for (u32 i = 0; i < parent_child_pairs.length; i++) {
        Parent_Child_Pair& pair = parent_child_pairs[i];

        Folder_Tree_Node* parent_node = pair.parent;
        Folder_Tree_Node* child_node = id_hash_map_get(&folder_id_to_node_map, pair.child_id, pair.child_hash);

        if (child_node) {
            found_pairs++;

            parent_node->children[parent_node->num_children] = child_node;
            parent_node->num_children++;
        }
    }

    printf("Found pairs %i\n", found_pairs);
}

static void build_folder_tree_search_index() {
    search_index = (char*) REALLOC(search_index, total_names_length);
    memset(search_index, 0, total_names_length);

    char* index_position = search_index;

    // Format is char length (or 0 if length exceeds u8) / char[] name
    for (u32 node_index = 0; node_index < total_nodes; node_index++) {
        Folder_Tree_Node* node = &all_nodes[node_index];
        String name = node->name;
        bool length_fits_char = name.length < 256;

        if (length_fits_char) {
            *index_position = (u8) name.length;
        }

        index_position++;

        for (u32 index = 0; index < name.length; index++) {
            index_position[index] = (char) tolower(name.start[index]);
        }

        index_position += node->name.length;

        if (!length_fits_char) {
            index_position++;
        }
    }
}

static void process_folder_tree_data(char* json, u32 data_size, jsmntok_t*& token) {
    all_nodes = (Folder_Tree_Node*) MALLOC(sizeof(Folder_Tree_Node) * data_size);
    total_nodes = data_size;
    current_node = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_tree_data_object(json, token);
    }
}

static void process_folder_contents_data_object(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Suggested_Folder* suggested_folder = &suggested_folders[suggested_folders.length++];

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, next_token, suggested_folder->name);
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, suggested_folder->id);
        } else if (json_string_equals(json, property_token, "color")) {
            String color;

            json_token_to_string(json, next_token, color);

            suggested_folder->color = string_to_folder_color(color);
        } else {
            eat_json(token);
            token--;
        }
    }
}

// TODO not a good signature, should we be leaving those in globals?
// TODO also we are managing char* json there, but not managing tokens!
void process_folder_tree_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (json_content) {
        FREE(json_content);
    }

    json_content = json;

    total_names_length = 0;

    process_json_data_segment(json, tokens, num_tokens, process_folder_tree_data);
    match_tree_parent_child_pairs();

    starred_nodes = (Folder_Tree_Node**) MALLOC(sizeof(Folder_Tree_Node*) * total_starred);

    for (u32 node_index = 0, starred_counter = 0; node_index < total_nodes; node_index++) {
        Folder_Tree_Node* node = &all_nodes[node_index];

        if (node->is_starred) {
            starred_nodes[starred_counter++] = node;
        }
    }

    lazy_array_clear(parent_child_pairs);

    build_folder_tree_search_index();
}

void process_suggested_folders_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (suggested_folders.length < data_size) {
        suggested_folders.data = (Suggested_Folder*) REALLOC(suggested_folders.data, sizeof(Suggested_Folder) * data_size);
    }

    suggested_folders.length = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_contents_data_object(json, token);
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

    const u32 ascii_number_start = 49;

#define char_at_to_index(at) *(string.start + (at)) - ascii_number_start

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