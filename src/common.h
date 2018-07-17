#include <imgui.h>
#include "xxhash.h"
#include "base32.h"
#include <stdio.h>

#pragma once

#define MALLOC(x) malloc_and_log(__FILE__, __FUNCTION__, __LINE__, x)
#define CALLOC(x, y) calloc_and_log(__FILE__, __FUNCTION__, __LINE__, x, y)
#define REALLOC(x, y) realloc_and_log(__FILE__, __FUNCTION__, __LINE__, x, y)
#define FREE(x) free_and_log(__FILE__, __FUNCTION__, __LINE__, x)

#define PRINTLIKE(string_index, first_to_check) __attribute__((__format__ (__printf__, string_index, first_to_check)))

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef char s8;
typedef long long s64;
typedef int s32;
typedef short s16;

typedef s32 Request_Id;

// TODO strong typedefs?
// TODO hash could be optionally contained within the ID
typedef s32 Account_Id;
typedef s32 Folder_Id;
typedef s32 Task_Id;
typedef s32 Custom_Field_Id;
typedef s32 Custom_Status_Id;
typedef s32 Workflow_Id;
typedef s32 User_Id;

static const u32 hash_seed = 3637;

u32 argb_to_agbr(u32 argb);

void* calloc_and_log(const char* file, const char* function, u32 line, size_t num, size_t size);
void* malloc_and_log(const char* file, const char* function, u32 line, size_t size);
void* realloc_and_log(const char* file, const char* function, u32 line, void* realloc_what, size_t new_size);
void free_and_log(const char* file, const char* function, u32 line, void* free_what);

const u32 color_background_dark = argb_to_agbr(0xFF1d364c);
const u32 color_link = argb_to_agbr(0xff4488ff);
const u32 color_black_text_on_white = 0xff191919;

struct Memory_Image {
    unsigned int texture_id = 0;
    unsigned width = 0;
    unsigned height = 0;
};

struct String {
    char* start = NULL;
    u32 length = 0;
};

template<typename T>
struct List {
    T* data = NULL;
    u32 length = 0;

    T& operator *() {
        return data;
    }

    T& operator [](const int index) {
        return *(data + index);
    }

    T* operator ->() {
        return *data;
    }
};

inline float lerp(float time_from, float time_to, float scale_to, float max) {
    float delta = (time_to - time_from);

    if (delta > max) {
        delta = max;
    }

    return ((scale_to / max) * delta);
}

void load_image_into_gpu_memory(Memory_Image& image, void* pixels);
bool load_png_from_disk(const char* path, Memory_Image& out);

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define GL_CHECKED(command)\
    command;\
    for(int error = glGetError(); (error=glGetError()); error != GL_NO_ERROR)\
    {\
        printf("glerror: %d\n", error);\
    }

inline bool are_strings_equal(String& a, String& b) {
    return a.length == b.length && strncmp(a.start, b.start, a.length) == 0;
}

inline u32 hash_string(String& string) {
    return XXH32(string.start, string.length, hash_seed);
}

inline u32 hash_id(s32 id) {
    return XXH32(&id, sizeof(id), hash_seed);
}

inline s32 uchars_to_s32(const u8* chars) {
    return (((chars[0]       ) << 24) |
            ((chars[1] & 0xff) << 16) |
            ((chars[2] & 0xff) <<  8) |
            ((chars[3] & 0xff)      ));
}

inline void fill_id8(const u8 type, s32 id, u8* output) {
    u8 input[] = {
            type,
            (u8) (id >> 24),
            (u8) (id >> 16),
            (u8) (id >> 8),
            (u8) id
    };

    base32_encode(input, ARRAY_SIZE(input), output);
}

inline void fill_id16(const u8 type1, s32 id1, const u8 type2, s32 id2, u8* output) {
    u8 input[] = {
            type1,
            (u8) (id1 >> 24),
            (u8) (id1 >> 16),
            (u8) (id1 >> 8),
            (u8) id1,
            type2,
            (u8) (id2 >> 24),
            (u8) (id2 >> 16),
            (u8) (id2 >> 8),
            (u8) id2
    };

    base32_encode(input, ARRAY_SIZE(input), output);
}

PRINTLIKE(1, 4) void tprintf(const char* format, char** start, char** end, ...);
PRINTLIKE(1, 2) String tprintf(const char* format, ...);

inline char* string_to_temporary_null_terminated_string(String string) {
    void* talloc(u32);

    char* folder_name = (char*) talloc(string.length + 1);

    memcpy(folder_name, string.start, string.length);
    folder_name[string.length] = 0;

    return folder_name;
}

// This is NOT performant, do not use often
template<typename T>
void add_item_to_list(List<T>& list, T item) {
    list.data = (T*) REALLOC(list.data, sizeof(T) * (list.length + 1));
    list[list.length++] = item;
}

template<typename T>
void remove_item_from_list_keep_order(List<T>& list, T item) {
    for (u32 index = 0; index < list.length; index++) {
        if (list[index] == item) {
            u32 remaining =  list.length - index;

            if (remaining) {
                memmove(list.data + index, list.data + index + 1, sizeof(T) * remaining);
            }

            list.length--;

            break;
        }
    }
}

s32 hackenstein(const char* a, const char* b, u32 a_length, u32 b_length);
s32 string_atoi(String* string);
s8* string_in_substring(const s8* big, const s8* small, size_t slen);