#include "common.h"
#include "jsmn.h"
#include "json.h"
#include "lazy_array.h"

#pragma once

struct Folder_Tree_Node {
    Folder_Id id;
    u32 id_hash;

    u16 num_children;
    Relative_Pointer<Folder_Tree_Node*> children;
    String name;

    bool is_starred;
};

void folder_tree_init();
void process_folder_tree_request(char* json, jsmntok_t* tokens, u32 num_tokens);
Folder_Tree_Node* find_folder_tree_node_by_id(Folder_Id id, u32 id_hash = 0);

extern Folder_Tree_Node* root_node;
extern Folder_Tree_Node* all_nodes;
extern Folder_Tree_Node** starred_nodes;
extern u32 total_nodes;
extern u32 total_starred;