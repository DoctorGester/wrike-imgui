#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include "rich_text.h"
#include "temporary_storage.h"

enum Rich_Text_Token_Type {
    Rich_Text_Token_Type_Tag,
    Rich_Text_Token_Type_Text,
    Rich_Text_Token_Type_Attribute,
    Rich_Text_Token_Type_Value
};

struct Rich_Text_Token {
    Rich_Text_Token_Type type;
    char* start;
    char* end = NULL;
    u32 num_attributes = 0;
    bool singular_attribute = true;
};

template<typename T>
struct Array {
    T* values = NULL;
    u32 length = 0;
    u32 watermark = 0;
};

template <typename T>
void array_try_resize(Array<T>* array) {
    if (array->watermark == array->length) {
        u32 previous_watermark = array->watermark;

        if (array->watermark == 0) {
            array->watermark = 2;
        } else {
            array->watermark *= 2;
        }

        array->values = (T*) trealloc(array->values, previous_watermark, sizeof(T) * array->watermark);
    }
}

template<typename T>
u32 array_add(Array<T>* array, T value) {
    array_try_resize(array);

    array->values[array->length] = value;

    return array->length++;
}

#define foreach(variable, array) \
    for(auto (variable) = (array).values, end = (array).values + (array).length; (variable) < end; (variable)++)

static Rich_Text_Token make_token(Rich_Text_Token_Type token_type, char* start) {
    Rich_Text_Token token{};
    token.type = token_type;
    token.start = start;
    token.num_attributes = 0;

    return token;
}

// TODO it's terrible, a rewrite in something like re2c is absolutely required!
static void parse_text_into_tokens(String& text, Array<Rich_Text_Token>& tokens) {
    Rich_Text_Token current_token{};
    current_token.start = text.start;
    current_token.type = Rich_Text_Token_Type_Text;

    const s32 NO_VALUE = -1;

    s32 current_tag = NO_VALUE;
    s32 current_attribute = NO_VALUE;
    s32 current_value = NO_VALUE;
    bool in_tag_name = false;
    bool in_attribute_name = false;
    bool post_attribute_equals = false;
    bool in_attribute_value = false;
    bool in_quoted_string = false;

    for (u32 index = 0; index < text.length; index++) {
        char* current_pointer = (index + text.start);
        char current = *current_pointer;

        Rich_Text_Token* tag = tokens.values + current_tag;
        Rich_Text_Token* attribute = tokens.values + current_attribute;
        Rich_Text_Token* value = tokens.values + current_value;

        if (current_tag != NO_VALUE) {
            bool self_closing_tag = (current == '/' && index < text.length - 1 && *(current_pointer + 1) == '>');

            if (current == '>' || self_closing_tag) {
                if (!tag->end) {
                    tag->end = current_pointer;
                }

                if (current_attribute != NO_VALUE && !attribute->end) {
                    attribute->end = current_pointer;
                }

                if (current_value != NO_VALUE && !value->end) {
                    value->end = current_pointer;
                }

                current_tag = NO_VALUE;
                current_attribute = NO_VALUE;
                current_value = NO_VALUE;
                post_attribute_equals = false;
                in_attribute_name = false;
                in_attribute_value = false;
                in_quoted_string = false;
                current_token = make_token(Rich_Text_Token_Type_Text, current_pointer + 1);

                if (self_closing_tag) {
                    current_token.start++;
                    index++;
                }
            } else if (current == '=' && current_attribute != NO_VALUE && !post_attribute_equals) {
                attribute->end = current_pointer;
                attribute->singular_attribute = false;

                in_attribute_name = false;
                post_attribute_equals = true;
            } else {
                if (in_tag_name) {
                    if (current == ' ') {
                        in_tag_name = false;
                        tag->end = current_pointer;
                    }
                } else {
                    if (in_attribute_name) {
                        if (current == ' ') {
                            attribute->end = current_pointer;
                            in_attribute_name = false;
                        }
                    } else {
                        if (post_attribute_equals) {
                            if (in_attribute_value) {
                                if (in_quoted_string) {
                                    if (current == '"') {
                                        in_attribute_value = false;
                                        post_attribute_equals = false;
                                        value->end = current_pointer + 1;
                                        in_quoted_string = false;
                                    }
                                } else {
                                    if (current == ' ') {
                                        in_attribute_value = false;
                                        post_attribute_equals = false;
                                        value->end = current_pointer;
                                    } else if (current == '"') {
                                        in_quoted_string = true;
                                    }
                                }
                            } else {
                                in_attribute_value = true;
                                current_token = make_token(Rich_Text_Token_Type_Value, current_pointer);
                                current_value = array_add(&tokens, current_token);
                            }
                        } else {
                            current_token = make_token(Rich_Text_Token_Type_Attribute, current_pointer);

                            in_attribute_name = true;

                            tag->num_attributes++;
                            current_attribute = array_add(&tokens, current_token);
                        }
                    }
                }
            }
        } else {
            if (current == '<') {
                if (current_pointer - current_token.start > 0) {
                    current_token.end = current_pointer;

                    array_add(&tokens, current_token);
                }

                current_token = make_token(Rich_Text_Token_Type_Tag, current_pointer + 1);

                in_tag_name = true;

                current_tag = array_add(&tokens, current_token);
            }
        }
    }

    char* text_end = (text.start + text.length);

    if (text_end - current_token.start > 0) {
        current_token.end = text_end;

        array_add(&tokens, current_token);
    }
}

bool token_eq(Rich_Text_Token* token, const char* text) {
    u32 length = MAX(token->end - token->start, strlen(text));
    return strncmp(token->start, text, length) == 0;
}

void parse_text_into_rich_string_recursively(Array<Rich_Text_Token>& tokens, Rich_Text_Style style, u32& index, Array<Rich_Text_String>* output) {
    for (; index < tokens.length; index++) {
        Rich_Text_Token& token = tokens.values[index];

        switch (token.type) {
            case Rich_Text_Token_Type_Text: {
                Rich_Text_String new_string;

                new_string.text.start = token.start;
                new_string.text.length = token.end - token.start;
                new_string.style = style;

                array_add(output, new_string);

                break;
            }

            case Rich_Text_Token_Type_Tag: {
                if (*token.start == '/') {
                    return;
                }

                Rich_Text_Style new_style = style;

                enum Tag_Type {
                    Tag_Type_A,
                    Tag_Type_Span,
                    Tag_Type_Img,
                    Tag_Type_List_Item,
                    Tag_Type_Other,
                    Tag_Type_Any
                };

                Tag_Type tag_type = Tag_Type_Other;

                if (token_eq(&token, "strong")) {
                    new_style.flags |= Rich_Text_Flags_Bold;
                    tag_type = Tag_Type_Any;
                } else if (token_eq(&token, "em")) {
                    new_style.flags |= Rich_Text_Flags_Italic;
                    tag_type = Tag_Type_Any;
                } else if (token_eq(&token, "br")) {
                    Rich_Text_String empty_string{};
                    array_add(output, empty_string);

                    continue;
                } else if (token_eq(&token, "img")) {
                    tag_type = Tag_Type_Img;
                } else if (token_eq(&token, "span")) {
                    tag_type = Tag_Type_Span;
                } else if (token_eq(&token, "a")) {
                    tag_type = Tag_Type_A;
                } else if (token_eq(&token, "ul")) {
                    tag_type = Tag_Type_Any;
                    new_style.list_depth++;
                } else if (token_eq(&token, "li")) {
                    tag_type = Tag_Type_List_Item;
                }

                for (u32 attribute_index = 0; attribute_index < token.num_attributes; attribute_index++) {
                    Rich_Text_Token* attribute = &tokens.values[++index];
                    Rich_Text_Token* value = NULL;

                    if (!attribute->singular_attribute) {
                        value = &tokens.values[++index];
                    }

                    switch (tag_type) {
                        case Tag_Type_A: {
                            if (token_eq(attribute, "href")) {
                                assert(value);

                                new_style.link.start = value->start;
                                new_style.link.length = value->end - value->start;
                            }

                            break;
                        }

                        case Tag_Type_Img: {
                            if (token_eq(attribute, "src")) {
                                assert(value);

                                // TODO image processing
                            }

                            break;
                        }

                        case Tag_Type_Span: {
                            // TODO we need to parse internal css there and extract background-color, bleh!
                            if (token_eq(attribute, "style")) {
                                assert(value);

                                for (char* c = value->start; c <= value->end; c++) {
                                    if (*c == '#') {
                                        char* start_ptr = c + 1;
                                        char* end_ptr = start_ptr + 6;
                                        new_style.background_color = argb_to_agbr(strtoul(start_ptr, &end_ptr, 16) | IM_COL32_A_MASK);

                                        // White background is "no color"
                                        if (new_style.background_color == 0xFFFFFFFF) {
                                            new_style.background_color = 0;
                                        }

                                        break;
                                    }
                                }
                            }

                            break;
                        }

                        case Tag_Type_Other: {
                            break;
                        }
                    }
                }

                if (tag_type == Tag_Type_Img) {
                    continue;
                }

                parse_text_into_rich_string_recursively(tokens, new_style, ++index, output);

                if (tag_type == Tag_Type_List_Item) {
                    Rich_Text_String empty_string{};
                    array_add(output, empty_string);
                }

                if (tag_type == Tag_Type_Other) {
                    printf("Unrecognized tag: %.*s\n", token.end - token.start, token.start);
                }

                assert(tokens.values[index].type == Rich_Text_Token_Type_Tag);

                break;
            }

            default: {
                assert(!"Unexpected token type");
            }
        }
    }
}

void destructively_strip_html_comments(String& text) {
    if (text.length == 0) {
        return;
    }

    char* comment_start = NULL;
    char* text_end = text.start + text.length;
    char* current;

    for (current = text.start; current < text_end; current++) {
        if (comment_start) {
            if (text_end - current >= 3 && strncmp("-->", current, 3) == 0) {
                char* post_comment_close_tag = current + 3;
                u32 remaining_text_length = text_end - post_comment_close_tag;
                u32 comment_length = post_comment_close_tag - comment_start;

                // Important to use memmove, the memory regions overlap!
                memmove(comment_start, post_comment_close_tag, remaining_text_length);

                text_end -= comment_length;
                current = comment_start;
                comment_start = NULL;
            }
        } else {
            if (text_end - current >= 4 && strncmp("<!--", current, 4) == 0) {
                comment_start = current;
            }
        }
    }

    text.length = text_end - text.start;
}


void parse_rich_text(String& text, Rich_Text_String*& output, u32& output_size) {
    Array<Rich_Text_Token> tokens{};
    parse_text_into_tokens(text, tokens);

    u32 text_tokens = 0;

    foreach(token, tokens) {
        if (token->type == Rich_Text_Token_Type_Text) {
            text_tokens++;
        }

        if (token->type == Rich_Text_Token_Type_Tag && token_eq(token, "br")) {
            text_tokens++;
        }

        if (token->type == Rich_Text_Token_Type_Tag && token_eq(token, "li")) {
            text_tokens++;
        }
    }

    // TODO that's a memory leak right there.
    // TODO a proper solution would be to use output and output_size and realloc them
    Array<Rich_Text_String> result_strings{};
    result_strings.values = (Rich_Text_String*) malloc(sizeof(Rich_Text_String) * text_tokens);
    result_strings.watermark = text_tokens;

    u32 index = 0;
    Rich_Text_Style empty_style{};

    parse_text_into_rich_string_recursively(tokens, empty_style, index, &result_strings);

//    foreach(str, result_strings) {
//        printf("%i :: %.*s\n", str->style.background_color, str->text.length, str->text.start);
//    }

    output = result_strings.values;
    output_size = text_tokens;
}