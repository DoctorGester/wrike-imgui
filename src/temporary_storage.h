#include <cstddef>
#include "common.h"

void clear_temporary_storage();
void temporary_storage_mark();
void temporary_storage_reset();
void* talloc(u32 size);
void* trealloc(void* pointer, u32 previous_size, u32 new_size);