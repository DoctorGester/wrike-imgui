#include <cstddef>

void init_temporary_storage();
void clear_temporary_storage();
void* talloc(size_t size);
void* trealloc(void* pointer, size_t previous_size, size_t new_size);