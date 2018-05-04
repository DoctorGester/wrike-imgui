#include <imgui.h>
#include "xxhash.h"
#include "base32.h"

#pragma once

typedef unsigned long long u64;
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long s64;
typedef long s32;
typedef short s16;

// TODO strong typedefs?
// TODO hash could be optionally contained within the ID
typedef s32 Account_Id;
typedef s32 Folder_Id;
typedef s32 Task_Id;
typedef s32 Custom_Field_Id;
typedef s32 Custom_Status_Id;
typedef s32 User_Id;

static const u32 hash_seed = 3637;

u32 argb_to_agbr(u32 argb);

#pragma once

const u32 color_background_dark = argb_to_agbr(0xFF284159);
const u32 color_link = argb_to_agbr(0x004488ff);

struct Memory_Image {
    unsigned int texture_id;
    unsigned width;
    unsigned height;
};

struct String {
    char* start = NULL;
    u32 length = 0;
};

inline float lerp(float time_from, float time_to, float scale_to, float max) {
    float delta = (time_to - time_from);

    if (delta > max) {
        delta = max;
    }

    return ((scale_to / max) * delta);
}

bool load_png_from_disk(const char* path, Memory_Image& out);

#define MIN(a, b) (a) < (b) ? (a) : (b)
#define MAX(a, b) (a) > (b) ? (a) : (b)
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
            (u8) (id << 24),
            (u8) (id << 16),
            (u8) (id << 8),
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

s32 string_atoi(String* string);