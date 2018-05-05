#include <cstddef>
#include "common.h"

void init_temporary_storage();
void clear_temporary_storage();
void* talloc(u32 size);
void* trealloc(void* pointer, u32 previous_size, u32 new_size);