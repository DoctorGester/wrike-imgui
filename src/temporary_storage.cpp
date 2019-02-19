#include <cstdlib>
#include "temporary_storage.h"

static const u32 available_memory_bytes = 1024 * 1024 * 8;

static char pointer_initial[available_memory_bytes];
static char* pointer_current = pointer_initial;
static char* pointer_previous = nullptr;
static char* pointer_mark = nullptr;

void clear_temporary_storage() {
    pointer_current = pointer_initial;
}

void temporary_storage_mark() {
    pointer_mark = pointer_current;
}

void temporary_storage_reset() {
    pointer_current = pointer_mark;
}

void* talloc(u32 size) {
    if ((pointer_current - pointer_initial) + size > available_memory_bytes) {
        void* heap_pointer = MALLOC(size);

        printf("Requested allocation of %i bytes which exceeded %i size of temporary storage, %p was allocated on heap\n",
               size, available_memory_bytes, heap_pointer);

        return heap_pointer;
    }

    size = (size + 4) & ~0x03; // Align, &~0x03 simply sets two trailing bits to 0

    pointer_previous = pointer_current;
    pointer_current += size;

    return pointer_previous;
}

void* trealloc(void* pointer, u32 previous_size, u32 new_size) {
    if (!pointer) {
        return talloc(new_size);
    }

    if (pointer == pointer_previous) {
        pointer_current = pointer_previous;
    } else {
        memcpy(pointer_current, pointer, previous_size);
    }

    return talloc(new_size);
}
