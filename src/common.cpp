#include <cstdio>
#include <lodepng.h>
#include <GLES2/gl2.h>
#include "common.h"
#include "temporary_storage.h"

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
template <typename T>
void test_template2(T t) {
    printf("%i", sizeof(t));
}
static void load_image_into_gpu_memory(Memory_Image& image, void* pixels) {
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

    free(data);

    return true;
}