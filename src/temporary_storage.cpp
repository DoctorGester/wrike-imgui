#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "temporary_storage.h"
#include "common.h"

static char* pointer_initial = nullptr;
static char* pointer_current = nullptr;
static char* pointer_previous = nullptr;

static const size_t available_memory_bytes = 1024 * 1024 * 16;

void init_temporary_storage() {
    pointer_initial = static_cast<char*>(MALLOC(available_memory_bytes));
    pointer_current = pointer_initial;
}

void clear_temporary_storage() {
    pointer_current = pointer_initial;
}

void* talloc(size_t size) {
    assert((pointer_current - pointer_initial) + size < available_memory_bytes);

    size = (size + 4) & ~0x03; // Align, &~0x03 simply sets two trailing bits to 0

    pointer_previous = pointer_current;
    pointer_current += size;

    return pointer_previous;
}

void* trealloc(void* pointer, size_t previous_size, size_t new_size) {
    if (!pointer) {
        return talloc(new_size);;
    }

    if (pointer == pointer_previous) {
        pointer_current = pointer_previous;
    } else {
        memcpy(pointer_current, pointer, previous_size);
    }

    return talloc(new_size);
}
