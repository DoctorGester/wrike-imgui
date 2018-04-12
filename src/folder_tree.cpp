#include "folder_tree.h"
#include "xxhash.h"
#include "jsmn.h"
#include "hash_map.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static Parent_Child_Pair* parent_child_pairs = NULL;
static u32 parent_child_pairs_count = 0;
static u32 parent_child_pairs_watermark = 0;

static Hash_Map<Folder_Tree_Node*> folder_id_to_node_map;

static u32 current_node = 0;

static char* json_content = NULL;

Folder_Tree_Node* root_node = NULL;
Folder_Tree_Node* all_nodes;
Folder_Tree_Node** starred_nodes = NULL;
u32 total_nodes;
u32 total_starred = 0;

void folder_tree_init() {
    hash_map_init(&folder_id_to_node_map, 2048);
}

inline Folder_Tree_Node* find_folder_tree_node_by_id(String& id, u32 hash) {
    return hash_map_get(&folder_id_to_node_map, id, hash);
}

static void add_parent_child_pair(String& parent_id, String& child_id) {
    if (parent_child_pairs_count == parent_child_pairs_watermark) {
        if (parent_child_pairs_watermark == 0) {
            parent_child_pairs_watermark = 64;
        } else {
            parent_child_pairs_watermark *= 2;
        }

        parent_child_pairs = (Parent_Child_Pair*) realloc(parent_child_pairs, sizeof(Parent_Child_Pair) * parent_child_pairs_watermark);
    }

    Parent_Child_Pair* pair = &parent_child_pairs[parent_child_pairs_count++];
    pair->parent = parent_id;
    pair->child = child_id;
    pair->parent_hash = hash_string(parent_id);
    pair->child_hash = hash_string(child_id);
}

static void process_folder_tree_data_object(jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    String name;
    String id;
    String scope;
    u32 num_children = 0;
    bool is_starred = false;

    bool has_id = false;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* value_token = token;

        // TODO string comparison there is inefficient, can be faster
        if (json_string_equals(json_content, property_token, "title")) {
            json_token_to_string(json_content, value_token, name);
        } else if (json_string_equals(json_content, property_token, "id")) {
            json_token_to_string(json_content, value_token, id);

            has_id = true;
        } else if (json_string_equals(json_content, property_token, "scope")) {
            json_token_to_string(json_content, value_token, scope);
        } else if (json_string_equals(json_content, property_token, "starred")) {
            assert(value_token->type == JSMN_PRIMITIVE);
            is_starred = *(json_content + value_token->start) == 't';
        } else if (json_string_equals(json_content, property_token, "childIds")) {
            assert(value_token->type == JSMN_ARRAY);
            assert(has_id);

            num_children = value_token->size;

            for (u32 array_index = 0; array_index < value_token->size; array_index++) {
                jsmntok_t* id_token = ++token;

                assert(id_token->type == JSMN_STRING);

                String child_id;

                json_token_to_string(json_content, id_token, child_id);
                add_parent_child_pair(id, child_id);
            }
        } else {
            eat_json(token);
            token--;
        }
    }

    u32 id_hash = hash_string(id);

    Folder_Tree_Node* new_node = &all_nodes[current_node++];
    new_node->id = id;
    new_node->name = name;
    new_node->id_hash = id_hash;
    new_node->num_children = 0;
    new_node->is_starred = is_starred;

    if (is_starred) {
        total_starred++;
    }

    if (num_children > 0) {
        new_node->children = (Folder_Tree_Node**) malloc(sizeof(Folder_Tree_Node*) * num_children);
    }

    hash_map_put(&folder_id_to_node_map, new_node, id_hash);

    bool is_root = strlen("WsRoot") == scope.length && strncmp(scope.start, "WsRoot", scope.length) == 0;

    if (is_root) {
        root_node = new_node;
    }
}

void match_tree_parent_child_pairs() {
    printf("Total pairs: %lu\n", parent_child_pairs_count);

    u32 found_pairs = 0;

    for (u32 i = 0; i < parent_child_pairs_count; i++) {
        Parent_Child_Pair& pair = parent_child_pairs[i];

        Folder_Tree_Node* parent_node = find_folder_tree_node_by_id(pair.parent, pair.parent_hash);
        Folder_Tree_Node* child_node = find_folder_tree_node_by_id(pair.child, pair.child_hash);

        if (parent_node && child_node) {
            found_pairs++;

            parent_node->children[parent_node->num_children] = child_node;
            parent_node->num_children++;
        }
    }

    printf("Found pairs %lu\n", found_pairs);
}

static void process_folder_tree_data(char* json, u32 data_size, jsmntok_t*& token) {
    all_nodes = (Folder_Tree_Node*) malloc(sizeof(Folder_Tree_Node) * data_size);
    total_nodes = data_size;
    current_node = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_tree_data_object(token);
    }
}

// TODO not a good signature, should we be leaving those in globals?
// TODO also we are managing char* json there, but not managing tokens!
void process_folder_tree_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    json_content = json;

    process_json_data_segment(json, tokens, num_tokens, process_folder_tree_data);
    match_tree_parent_child_pairs();

    starred_nodes = (Folder_Tree_Node**) malloc(sizeof(Folder_Tree_Node*) * total_starred);

    for (u32 node_index = 0, starred_counter = 0; node_index < total_nodes; node_index++) {
        Folder_Tree_Node* node = &all_nodes[node_index];

        if (node->is_starred) {
            starred_nodes[starred_counter++] = node;
        }
    }
}