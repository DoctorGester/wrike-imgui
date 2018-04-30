#include "common.h"
#include "jsmn.h"
#include "json.h"

#pragma once

struct Folder_Tree_Node {
    u16 num_children;
    u16 children_limit;
    Folder_Tree_Node** children;
    String name;
    u32 id_hash;
    // TODO I believe folder ids could be kept right in the structure without indirection bc they are always of the same size
    String id;

    bool is_starred;
};

void folder_tree_init();
void process_folder_tree_request(char* json, jsmntok_t* tokens, u32 num_tokens);

extern Folder_Tree_Node* root_node;
extern Folder_Tree_Node* all_nodes;
extern Folder_Tree_Node** starred_nodes;
extern u32 total_nodes;
extern u32 total_starred;