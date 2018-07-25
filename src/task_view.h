#include "main.h"

void draw_task_contents();
void process_task_data(char* json, u32 data_size, jsmntok_t*& token);
void process_task_comments_data(char* json, u32 data_size, jsmntok_t*& token);
void process_task_custom_field_value(Custom_Field_Value* custom_field_value, char* json, jsmntok_t*& token);