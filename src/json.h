#include "jsmn.h"
#include "common.h"
#include "temporary_storage.h"
#include <cstring>
#include <cstdio>

#pragma once

typedef void (*Data_Process_Callback)(char* json, u32 data_size, jsmntok_t*& token);

void json_token_to_string(char* json, jsmntok_t* token, String &string);
void eat_json(jsmntok_t*& token);
jsmntok_t* parse_json_into_tokens(char* content_json, u32& result_parsed_tokens);
void process_json_data_segment(char* json, jsmntok_t* tokens, u32 num_tokens, Data_Process_Callback callback);

inline bool json_string_equals(char* json, jsmntok_t* tok, const char *s) {
    u32 token_length = (u32) (tok->end - tok->start);

    return (u32) strlen(s) == token_length && strncmp(json + tok->start, s, token_length) == 0;
}

inline void json_token_to_id(char* json, jsmntok_t* token, Id16& id) {
    memcpy(id.id, json + token->start, ID_16_LENGTH);
}

inline void json_token_to_id(char* json, jsmntok_t* token, Id8& id) {
    memcpy(id.id, json + token->start, ID_8_LENGTH);
}

inline char* string_to_temporary_null_terminated_string(String& string) {
    char* node_name_null_terminated = (char*) talloc(string.length + 1);
    sprintf(node_name_null_terminated, "%.*s", string.length, string.start);

    return node_name_null_terminated;
}