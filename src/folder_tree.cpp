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

List<Folder_Tree_Node*> search_result{};

void folder_tree_init() {
    id_hash_map_init(&folder_id_to_node_map);
}

Folder_Tree_Node* find_folder_tree_node_by_id(Folder_Id id, u32 id_hash) {
    if (!id_hash) {
        id_hash = hash_id(id);
    }

    return id_hash_map_get(&folder_id_to_node_map, id, id_hash);
}

static inline Folder_Tree_Node* unsafe_find_folder_tree_node_by_id(Folder_Id id, u32 hash) {
    return id_hash_map_get(&folder_id_to_node_map, id, hash);
}

static void add_parent_child_pair(Folder_Tree_Node* parent, Folder_Id child_id) {
    Parent_Child_Pair* pair = lazy_array_reserve_n_values(parent_child_pairs, 1);
    pair->parent = parent;
    pair->child_id = child_id;
    pair->child_hash = hash_id(child_id);
}

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

            if (new_node->name.length > 255) {
                total_names_length++;
            }
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, value_token, new_node->id);
        } else if (json_string_equals(json, property_token, "scope")) {
            json_token_to_string(json, value_token, scope);
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

void folder_tree_search(char* query) {
    u32 query_length = strlen(query);

    if (!query_length) {
        return;
    }

    char* query_lowercase = (char*) talloc(query_length + 1);

    for (u32 index = 0; index < query_length; index++) {
        query_lowercase[index] = (char) tolower(query[index]);
    }

    query_lowercase[query_length] = 0;

    if (!search_result.data) {
        search_result.data = (Folder_Tree_Node**) MALLOC(sizeof(Folder_Tree_Node*) * total_nodes);
    }

    search_result.length = 0;

    char* it = search_index;

    for (u32 node_index = 0; node_index < total_nodes; node_index++) {
        u32 length_or_zero = (u8) *it;

        it++;

        if (!length_or_zero) {
            length_or_zero = (u32) strlen(it);
        }

        if (string_in_substring(it, query_lowercase, length_or_zero)) {
            search_result[search_result.length++] = &all_nodes[node_index];
        }

        it += length_or_zero;
    }
}

static void match_tree_parent_child_pairs() {
    printf("Total pairs: %i\n", parent_child_pairs.length);

    u32 found_pairs = 0;

    for (u32 i = 0; i < parent_child_pairs.length; i++) {
        Parent_Child_Pair& pair = parent_child_pairs[i];

        Folder_Tree_Node* parent_node = pair.parent;
        Folder_Tree_Node* child_node = unsafe_find_folder_tree_node_by_id(pair.child_id, pair.child_hash);

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