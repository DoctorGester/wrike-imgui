#include "common.h"
#include "json.h"
#include "platform.h"
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

static jsmntok_t* parse_json_iteratively(const char* json, u32 json_length, s32 &num_tokens) {
    jsmn_parser parser;
    jsmn_init(&parser);

    jsmntok_t* tokens;

    u32 token_watermark = 16;

    tokens = (jsmntok_t*) talloc(sizeof(jsmntok_t) * token_watermark);

    if (tokens == NULL) {
        return NULL;
    }

    try_read_tokens: {
        s32 return_code = jsmn_parse(&parser, json, json_length, tokens, token_watermark);

        if (return_code < 0) {
            if (return_code == JSMN_ERROR_NOMEM) {
                u32 prev_count = token_watermark;
                token_watermark = token_watermark * 2;
                tokens = (jsmntok_t*) trealloc(
                        tokens,
                        sizeof(jsmntok_t) * prev_count,
                        sizeof(jsmntok_t) * token_watermark
                );

                if (tokens == NULL) {
                    return NULL;
                }

                goto try_read_tokens;
            }
        } else {
            num_tokens = return_code;
            return tokens;
        }
    }

    return NULL;
}

jsmntok_t* parse_json_into_tokens(char* content_json, u32 json_length, u32& result_parsed_tokens) {
    float start_time = platform_get_app_time_ms();

    s32 num_tokens = 0;
    jsmntok_t* json_tokens = parse_json_iteratively(content_json, json_length, num_tokens);

    assert(num_tokens > 0);

    u32 parsed_tokens = (u32) num_tokens;

    printf("Parsed %lu tokens in %fms\n", parsed_tokens, platform_get_app_time_ms() - start_time);

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