void draw_task_list();
void set_current_folder_id(Folder_Id id);
void process_current_folder_as_logical();
void process_folder_contents_data(char* json, u32 data_size, jsmntok_t*& token);
void process_folder_header_data(char* json, u32 data_size, jsmntok_t*& token);