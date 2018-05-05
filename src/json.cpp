#include "common.h"
#include "json.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>

void json_token_to_string(char* json, jsmntok_t* token, String &string) {
    string.start = json + token->start;
    string.length = token->end - token->start;
}

void eat_json(jsmntok_t*& token) {
    jsmntok_t* current_token = token++;

    switch (current_token->type) {
        case JSMN_STRING:
        case JSMN_PRIMITIVE: {
            break;
        }

        case JSMN_ARRAY: {
            for (u32 i = 0; i < current_token->size; i++) eat_json(token);
            break;
        }

        case JSMN_OBJECT: {
            for (u32 i = 0; i < current_token->size; i++) {
                eat_json(token);
                eat_json(token);
            }

            break;
        }

        case JSMN_UNDEFINED: break;
    }
}

jsmntok_t* parse_json_into_tokens(char* content_json, u32& result_parsed_tokens) {
    jsmn_parser json_parser;
    jsmn_init(&json_parser);

    s32 needs_tokens = jsmn_parse(&json_parser, content_json, strlen(content_json), 0, 10);

    assert(needs_tokens > 0);

    u32 num_tokens = (u32) needs_tokens;

    // TODO what happens if temporary storage size is simply not enough?
    jsmntok_t* json_tokens = (jsmntok_t*) talloc(sizeof(jsmntok_t) * needs_tokens);

    jsmn_init(&json_parser);
    int parsed_tokens = jsmn_parse(&json_parser, content_json, strlen(content_json), json_tokens, num_tokens);

    assert(parsed_tokens > 0);

    printf("Parsed %i tokens\n", parsed_tokens);

    result_parsed_tokens = (u32) parsed_tokens;

    return json_tokens;
}


void process_json_data_segment(char* json, jsmntok_t* tokens, u32 num_tokens, Data_Process_Callback callback) {
    jsmntok_t* end_token = tokens + num_tokens;

    for (jsmntok_t* start_token = tokens; start_token < end_token; start_token++) {
        if (json_string_equals(json, start_token, "data")) {
            jsmntok_t* next_token = ++start_token;

            assert(next_token->type == JSMN_ARRAY);

            start_token++;

            callback(json, (u32) next_token->size, start_token);
        }
    }
}