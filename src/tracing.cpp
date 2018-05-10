#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include "common.h"

struct Memory_Record {
    void* pointer;
    size_t size;
    u32 line;
    const char* function;
    const char* file;
};

static u32 history_length = 0;
static u32 history_watermark = 0;
static Memory_Record* memory_records = NULL;

static u32 total_allocated_memory = 0;

static void bytes_to_human_readable_size(size_t bytes, float& out_size, const char*& out_unit) {
    static const char* sizes[] = { "B", "kB", "MB", "GB" };
    size_t div = 0;
    size_t rem = 0;

    while (bytes >= 1024 && div < (sizeof sizes / sizeof *sizes)) {
        rem = (bytes % 1024);
        div++;
        bytes /= 1024;
    }

    out_unit = sizes[div];
    out_size = (float)bytes + (float)rem / 1024.0f;
}

void log_record(Memory_Record& record) {
    const char* last_slash = strrchr(record.file, '/');
    const char* file = last_slash ? (last_slash + 1) : record.file;

    const char* unit = "";
    float size = 0.0f;

    bytes_to_human_readable_size(record.size, size, unit);

    ImGui::Text("%s:%i %s %p %.1f %s", file, record.line, record.function, record.pointer, size, unit);
}

void draw_memory_records() {
    const char* unit = "";
    float size = 0.0f;

    bytes_to_human_readable_size(total_allocated_memory, size, unit);

    ImGui::Text("Total memory occupied: %.1f %s", size, unit);
    ImGui::Text("Total blocks: %i", history_length);

    for (u32 index = 0; index < history_length; index++) {
        log_record(memory_records[index]);
    }
}

static void record_memory(void* pointer, const char* file, const char* function, u32 line, size_t size) {
    Memory_Record record;
    record.pointer = pointer;
    record.size = size;
    record.file = file;
    record.line = line;
    record.function = function;

    if (history_length == history_watermark) {
        history_watermark += 1000;
        memory_records = (Memory_Record*) realloc(memory_records, sizeof(Memory_Record) * history_watermark);
    }

    memory_records[history_length++] = record;
}

/*
void draw_memory_records() {
    const u32 current_tail = total_recorded % history_length;

    if (total_recorded > history_length){
        for (u32 index = current_tail; index < history_length; index++) {
            log_record(memory_records[index]);
        }
    }

    for (u32 index = 0; index < current_tail; index++) {
        log_record(memory_records[index]);
    }
}

static void record_memory(void* pointer, const char* file, u32 line, size_t size) {
    Memory_Record record;
    record.pointer = pointer;
    record.size = size;
    record.file = file;
    record.line = line;

    memory_records[total_recorded % history_length] = record;
    total_recorded++;
}*/

void* malloc_and_log(const char* file, const char* function, u32 line, size_t size) {
    void* pointer = malloc(size);

    total_allocated_memory += size;

    record_memory(pointer, file, function, line, size);

    return pointer;
}

void* calloc_and_log(const char* file, const char* function, u32 line, size_t num, size_t size) {
    void* pointer = calloc(num, size);

    total_allocated_memory += size * num;

    record_memory(pointer, file, function, line, size * num);

    return pointer;
}

void* realloc_and_log(const char* file, const char* function, u32 line, void* realloc_what, size_t new_size) {
    if (realloc_what) {
        void* pointer = realloc(realloc_what, new_size);

        for (u32 index = 0; index < history_length; index++) {
            Memory_Record& old_record = memory_records[index];

            if (old_record.pointer == realloc_what) {
                old_record.pointer = pointer;

                total_allocated_memory -= old_record.size;
                total_allocated_memory += new_size;

                old_record.size = new_size;
                old_record.file = file;
                old_record.line = line;

                return pointer;
            }
        }

        printf("WARNING: Reallocation of an unmanaged pointer %p with size %i at %s %s:%i\n", realloc_what, new_size, function, file, line);

        return pointer;
    } else {
        return malloc_and_log(file, function, line, new_size);
    }
}

void free_and_log(const char* file, const char* function, u32 line, void* free_what) {
    free(free_what);

    for (u32 index = 0; index < history_length; index++) {
        Memory_Record& old_record = memory_records[index];

        if (old_record.pointer == free_what) {
            total_allocated_memory -= old_record.size;

            if (history_length > 1) {
                old_record = memory_records[history_length - 1];
            }

            history_length--;

            return;
        }
    }

    printf("WARNING: Freeing of an unmanaged pointer %p at %s %s:%i\n", free_what, function, file, line);
}