#include <imgui.h>
#include "xxhash.h"

#pragma once

typedef unsigned long long u64;
typedef unsigned long u32;
typedef unsigned short u16;
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

bool load_png_from_disk(const char* path, Memory_Image& out);

#define MAX(a, b) (a) > (b) ? (a) : (b)
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define GL_CHECKED(command)\
    command;\
    for(int error = glGetError(); (error=glGetError()); error != GL_NO_ERROR)\
    {\
        emscripten_log(EM_LOG_CONSOLE|EM_LOG_C_STACK|EM_LOG_DEMANGLE, "glerror: %d", error);\
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

static inline ImVec2 operator*(const ImVec2& lhs, const float rhs)              { return ImVec2(lhs.x*rhs, lhs.y*rhs); }
static inline ImVec2 operator/(const ImVec2& lhs, const float rhs)              { return ImVec2(lhs.x/rhs, lhs.y/rhs); }
static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)            { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)            { return ImVec2(lhs.x-rhs.x, lhs.y-rhs.y); }
static inline ImVec2 operator*(const ImVec2& lhs, const ImVec2& rhs)            { return ImVec2(lhs.x*rhs.x, lhs.y*rhs.y); }
static inline ImVec2 operator/(const ImVec2& lhs, const ImVec2& rhs)            { return ImVec2(lhs.x/rhs.x, lhs.y/rhs.y); }
static inline ImVec2& operator+=(ImVec2& lhs, const ImVec2& rhs)                { lhs.x += rhs.x; lhs.y += rhs.y; return lhs; }
static inline ImVec2& operator-=(ImVec2& lhs, const ImVec2& rhs)                { lhs.x -= rhs.x; lhs.y -= rhs.y; return lhs; }
static inline ImVec2& operator*=(ImVec2& lhs, const float rhs)                  { lhs.x *= rhs; lhs.y *= rhs; return lhs; }
static inline ImVec2& operator/=(ImVec2& lhs, const float rhs)                  { lhs.x /= rhs; lhs.y /= rhs; return lhs; }
static inline ImVec4 operator+(const ImVec4& lhs, const ImVec4& rhs)            { return ImVec4(lhs.x+rhs.x, lhs.y+rhs.y, lhs.z+rhs.z, lhs.w+rhs.w); }
static inline ImVec4 operator-(const ImVec4& lhs, const ImVec4& rhs)            { return ImVec4(lhs.x-rhs.x, lhs.y-rhs.y, lhs.z-rhs.z, lhs.w-rhs.w); }
static inline ImVec4 operator*(const ImVec4& lhs, const ImVec4& rhs)            { return ImVec4(lhs.x*rhs.x, lhs.y*rhs.y, lhs.z*rhs.z, lhs.w*rhs.w); }