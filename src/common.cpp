#include <cstdio>
#include "common.h"
#include "temporary_storage.h"
#include "tracing.h"
#include "platform.h"
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <cerrno>

u32 argb_to_agbr(u32 argb) {
    u32 a = argb & 0xFF000000;
    u32 r = argb & 0x00FF0000;
    u32 g = argb & 0x0000FF00;
    u32 b = argb & 0x000000FF;

    return a | (r >> 16) | g | (b << 16);
}

s32 string_atoi(String* string) {
    char* buf = string->start;
    u32 len = string->length;

    s32 n = 0, sign = 1;

    if (len)
        switch (*buf) {
            case '-': sign = -1;
            case '+': --len, ++buf;
        }

    while (len-- && isdigit(*buf))
        n = n * 10 + *buf++ - '0';

    return n * sign;
}

char* read_file(const char* file_name, u32* out_length) {
    char* buffer = 0;
    long length;
    FILE* file_handle = fopen(file_name, "r");

    printf("Trying to read %s\n", file_name);

    if (!file_handle) {
        printf("Failed to read %s\n", file_name);
    }

    if (file_handle) {
        fseek(file_handle, 0, SEEK_END);
        length = ftell(file_handle);
        fseek(file_handle, 0, SEEK_SET);
        buffer = (char*) talloc(length); // TODO this won't work with big files

        if (buffer) {
            fread(buffer, 1, length, file_handle);
        }

        fclose(file_handle);

        *out_length = length;
    }

    return buffer;
}

static inline bool is_power_of_two(unsigned n) {
    return (n & (n - 1)) == 0;
}

void load_image_into_gpu_memory(Memory_Image& image, u8* pixels) {
    image.texture_id = platform_make_texture(image.width, image.height, pixels);
}

void load_png_from_disk_async(const char* path, Image_Load_Callback callback) {
    Array<u8> image_data;
    char* file_data = read_file(path, &image_data.length);

    if (file_data) {
        image_data.data = (u8*) file_data;

        platform_load_png_async(image_data, callback);
    }
}

s32 _min(s32 d0, s32 d1, s32 d2, s32 bx, s32 ay)
{
    return d0 < d1 || d2 < d1
           ? d0 > d2
             ? d2 + 1
             : d0 + 1
           : bx == ay
                    ? d1
                    : d1 + 1;
}

s32 hackenstein(const char* a, const char* b, u32 a_length, u32 b_length) {
    if (a == b) {
        return 0;
    }

    if (a_length > b_length) {
        const char* tmp_char = a;
        a = b;
        b = tmp_char;

        u32 tmp_l = a_length;
        a_length = b_length;
        b_length = tmp_l;
    }

    u32 la = a_length;
    u32 lb = b_length;

    // TODO can be done using char*?
    while (la > 0 && (a[la - 1] == b[lb - 1])) {
        la--;
        lb--;
    }

    u32 offset = 0;

    while (offset < la && (a[offset] == b[offset])) {
        offset++;
    }

    la -= offset;
    lb -= offset;

    if (la == 0 || lb == 1) {
        return lb;
    }

    s32 x = 0;
    s32 y;
    s32 d0;
    s32 d1;
    s32 d2;
    s32 d3;
    s32 dd = 0;
    s32 dy;
    s32 ay;
    s32 bx0;
    s32 bx1;
    s32 bx2;
    s32 bx3;

    static s32 vector[256];
    static u32 vector_head;

    vector_head = 0;

    for (y = 0; y < la; y++) {
        vector[vector_head++] = y + 1;
        vector[vector_head++] = a[offset + y];
    }

    for (; (x + 3) < lb;) {
        bx0 = b[offset + (d0 = x)];
        bx1 = b[offset + (d1 = x + 1)];
        bx2 = b[offset + (d2 = x + 2)];
        bx3 = b[offset + (d3 = x + 3)];
        dd = (x += 4);
        for (y = 0; y < vector_head; y += 2) {
            dy = vector[y];
            ay = vector[y + 1];
            d0 = _min(dy, d0, d1, bx0, ay);
            d1 = _min(d0, d1, d2, bx1, ay);
            d2 = _min(d1, d2, d3, bx2, ay);
            dd = _min(d2, d3, dd, bx3, ay);
            vector[y] = dd;
            d3 = d2;
            d2 = d1;
            d1 = d0;
            d0 = dy;
        }
    }
    for (; x < lb;) {
        bx0 = b[offset + (d0 = x)];
        dd = ++x;
        for (y = 0; y < vector_head; y += 2) {
            dy = vector[y];
            vector[y] = dd = dy < d0 || dd < d0
                             ? dy > dd ? dd + 1 : dy + 1
                             : bx0 == vector[y + 1]
                                        ? d0
                                        : d0 + 1;
            d0 = dy;
        }
    }

    return dd;
};

int levenshtein(const char* a, const char* b, u32 a_length, u32 b_length) {
    static int levenstein_cache[1024];

    assert(a_length <= ARRAY_SIZE(levenstein_cache));

    int index = 0;
    int bIndex = 0;
    int distance;
    int bDistance;
    int result;
    char code;

    /* Shortcut optimizations / degenerate cases. */
    if (a == b) {
        return 0;
    }

    if (a_length == 0) {
        return b_length;
    }

    if (b_length == 0) {
        return a_length;
    }

    /* initialize the vector. */
    while (index < a_length) {
        levenstein_cache[index] = index + 1;
        index++;
    }

    /* Loop. */
    while (bIndex < b_length) {
        code = b[bIndex];
        result = distance = bIndex++;
        index = -1;

        while (++index < a_length) {
            bDistance = code == a[index] ? distance : distance + 1;
            distance = levenstein_cache[index];

            levenstein_cache[index] = result = distance > result
                                               ? bDistance > result
                                                 ? result + 1
                                                 : bDistance
                                               : bDistance > distance
                                                 ? distance + 1
                                                 : bDistance;
        }
    }

    return result;
}

char* string_in_substring(const s8* big, const s8* small, size_t slen) {
    s8 c, sc;
    size_t len;

    if ((c = *small++) != '\0') {
        len = strlen(small);
        do {
            do {
                if ((sc = *big++) == '\0' || slen-- < 1)
                    return (NULL);
            } while (sc != c);
            if (len > slen)
                return (NULL);
        } while (strncmp(big, small, len) != 0);
        big--;
    }
    return ((char*) big);
}

PRINTLIKE(1, 0) String tprintf(const char* format, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    String result{};

    result.length = (u32) vsnprintf(NULL, 0, format, args_copy);

    va_end(args_copy);

    assert(result.length >= 0);

    result.start = (char*) talloc((u32) result.length + 1);

    vsnprintf(result.start, result.length + 1, format, args);

    return result;
}

PRINTLIKE(1, 2) String tprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    String result = tprintf(format, args);

    va_end(args);

    return result;
}

PRINTLIKE(1, 4) void tprintf(const char* format, char** start, char** end, ...) {
    va_list args;
    va_start(args, end);

    va_list args_copy;
    va_copy(args_copy, args);

    s32 length = vsnprintf(NULL, 0, format, args_copy);

    va_end(args_copy);

    assert(length >= 0);

    *start = (char*) talloc((u32) length + 1);
    *end = *start + length;

    vsnprintf(*start, length + 1, format, args);

    va_end(args);
}

string_to_int_error string_to_int(s32 *out, char *s, u32 base) {
    char *end;
    if (s[0] == '\0' || isspace(s[0]))
        return STR2INT_INCONVERTIBLE;
    errno = 0;
    long l = strtol(s, &end, base);
    /* Both checks are needed because INT_MAX == LONG_MAX is possible. */
    if (l > INT_MAX || (errno == ERANGE && l == LONG_MAX))
        return STR2INT_OVERFLOW;
    if (l < INT_MIN || (errno == ERANGE && l == LONG_MIN))
        return STR2INT_UNDERFLOW;
    if (*end != '\0')
        return STR2INT_INCONVERTIBLE;
    *out = l;
    return STR2INT_SUCCESS;
}