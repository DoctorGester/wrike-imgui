#include <cstdio>
#include <lodepng.h>
#if EMSCRIPTEN
#include <GLES2/gl2.h>
#else
#include <SDL2/SDL_opengl.h>
#endif
#include "common.h"
#include "temporary_storage.h"
#include "tracing.h"
#include "platform.h"
#include <string.h>

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

char* read_file(const char* file_name) {
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
        buffer = (char*) talloc(length + 1); // TODO this won't work with big files

        if (buffer) {
            fread(buffer, 1, length, file_handle);
        }

        *(buffer + length) = '\0';

        fclose(file_handle);
    }

    return buffer;
}

static inline bool is_power_of_two(unsigned n) {
    return (n & (n - 1)) == 0;
}

void load_image_into_gpu_memory(Memory_Image& image, void* pixels) {
    GLuint texture_id = 0;

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    //glGenerateMipmap(GL_TEXTURE_2D);

    image.texture_id = texture_id;
}

bool load_png_from_disk(const char* path, Memory_Image& out) {
    unsigned char *data;
    unsigned error = lodepng_decode32_file(&data, &out.width, &out.height, path);

    if (error) {
        return false;
    }

    load_image_into_gpu_memory(out, data);

    // TODO Not using a FREE macro because memory is coming from an outside library
    // TODO     but honestly we should just record that memory too
    free(data);

    return true;
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