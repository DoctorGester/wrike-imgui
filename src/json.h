#include "jsmn.h"
#include "common.h"
#include "temporary_storage.h"
#include <cstring>
#include <cstdio>
#include <jsmn.h>

#pragma once

typedef void (*Data_Process_Callback)(char* json, u32 data_size, jsmntok_t*& token);

void json_token_to_string(char* json, jsmntok_t* token, String &string);
void eat_json(jsmntok_t*& token);
jsmntok_t* parse_json_into_tokens(char* content_json, u32 json_length, u32& result_parsed_tokens);
void process_json_data_segment(char* json, jsmntok_t* tokens, u32 num_tokens, Data_Process_Callback callback);

inline bool json_string_equals(char* json, jsmntok_t* tok, const char *s) {
    u32 token_length = (u32) (tok->end - tok->start);

    return (u32) strlen(s) == token_length && strncmp(json + tok->start, s, token_length) == 0;
}

inline void json_token_to_right_part_of_id16(char* json, jsmntok_t* token, s32& id) {
    u8* token_start = (u8*) json + token->start;
    u8 result[UNBASE32_LEN(16)];

    // TODO wasteful to decode all bytes but only use some
    base32_decode(token_start, 16, result);

    id = uchars_to_s32(result + 6);
}

inline void json_token_to_id8(char* json, jsmntok_t* token, s32& id) {
    u8* token_start = (u8*) json + token->start;
    u8 result[UNBASE32_LEN(8)];

    base32_decode(token_start, 8, result);

    id = uchars_to_s32((u8*) result + 1);
}