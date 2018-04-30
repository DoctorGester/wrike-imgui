#include <imgui.h>
#include "xxhash.h"

#pragma once

typedef unsigned long long u64;
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long s64;
typedef long s32;
typedef short s16;

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

// TODO just remove those? Idk
const int ID_16_LENGTH = 16;
const int ID_8_LENGTH = 8;

struct Id16 {
    char id[ID_16_LENGTH];
};

struct Id8 {
    char id[ID_8_LENGTH];
};

inline bool are_ids_equal(Id16* a, Id16* b) {
    return memcmp(a->id, b->id, ID_16_LENGTH) == 0;
}

inline bool are_ids_equal(Id8* a, Id8* b) {
    return memcmp(a->id, b->id, ID_8_LENGTH) == 0;
}

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

// TODO hack because we need id as string in hash map
inline bool are_strings_equal(String& a, Id16& b) {
    return a.length == ID_16_LENGTH && strncmp(a.start, b.id, ID_16_LENGTH) == 0;
}

inline u32 hash_string(String& string) {
    return XXH32(string.start, string.length, hash_seed);
}

inline u32 hash_id(Id16& id) {
    return XXH32(id.id, ID_16_LENGTH, hash_seed);
}

s32 string_atoi(String* string);