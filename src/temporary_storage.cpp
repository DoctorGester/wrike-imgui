#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "temporary_storage.h"
#include "common.h"

static char* pointer_initial = nullptr;
static char* pointer_current = nullptr;
static char* pointer_previous = nullptr;

static const u32 available_memory_bytes = 1024 * 1024 * 4;

static void** temporary_heap_pointers = NULL;
static u32 num_temporary_heap_pointers = 0;

void init_temporary_storage() {
    pointer_initial = static_cast<char*>(MALLOC(available_memory_bytes));
    pointer_current = pointer_initial;
}

void clear_temporary_storage() {
    pointer_current = pointer_initial;

    void** head = temporary_heap_pointers;

    for (void** end = head + num_temporary_heap_pointers; head < end; head++) {
        printf("Freeing temporary allocated %p\n", *head);

        FREE(*head);
    }

    temporary_heap_pointers = NULL;
    num_temporary_heap_pointers = 0;
}

void* talloc(u32 size) {
    if ((pointer_current - pointer_initial) + size > available_memory_bytes) {
        u32 previous_size = num_temporary_heap_pointers++;

        temporary_heap_pointers = (void**) trealloc(
                temporary_heap_pointers,
                previous_size * sizeof(void*),
                num_temporary_heap_pointers * sizeof(void*)
        );

        void* heap_pointer = MALLOC(size);

        temporary_heap_pointers[previous_size] = heap_pointer;

        printf("Requested allocation of %lu bytes which exceeded %lu size of temporary storage, %p was allocated on heap\n",
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
