#include "common.h"
#include "jsmn.h"
#include "json.h"
#include "lazy_array.h"

#pragma once

struct Folder_Color {
    u32 background;
    u32 text;
    u32 background_hover;

    Folder_Color(u32 background, u32 text, u32 background_hover) {
        this->background = argb_to_agbr(background);
        this->text = argb_to_agbr(text);
        this->background_hover = argb_to_agbr(background_hover);
    }
};

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

struct Folder {
    Folder_Id id;
    String name;
    Folder_Color* color;
};

struct Folder_Tree_Node : Folder {
    u32 id_hash;
    Folder_Handle children;
    u32 num_children;
    u32 finished_loading_children_at;
    u32 num_expected_children;
    bool children_loaded;
    bool is_expanded;
};

const Folder_Handle NULL_FOLDER_HANDLE(-1);

void draw_folder_tree(float column_width);
void folder_tree_init(Folder_Id root_node_id);
void process_folder_tree_request(Folder_Id folder, char* json, jsmntok_t* tokens, u32 num_tokens);
void process_suggested_folders_data(char* json, u32 data_size, jsmntok_t*&token);
void process_starred_folders_data(char* json, u32 data_size, jsmntok_t*& token);

void folder_tree_search(const char* query, List<Folder_Tree_Node*>* result);

Folder_Tree_Node* find_folder_tree_node_by_id(Folder_Id id, u32 id_hash = 0);

extern List<Folder_Tree_Node> all_nodes;

extern List<Folder> suggested_folders;