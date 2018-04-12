#pragma once

#include <cstdlib>
#include "common.h"

template <typename T>
struct Hash_Bucket {
    T* contents;
    u32 current_size;
    u32 size_limit;
};

template <typename T>
struct Hash_Map {
    u32 total_buckets;
    u32 size;

    Hash_Bucket<T>* buckets;
};

template <typename T>
void hash_map_init(Hash_Map<T>* map, u32 total_buckets) {
    map->buckets = (Hash_Bucket<T>*) calloc(total_buckets, sizeof(Hash_Bucket<T>));
    map->total_buckets = total_buckets;
    map->size = 0;

    //memset(map->buckets, 0, sizeof(Hash_Bucket<T>) * total_buckets);
}

template <typename T>
void hash_map_destroy(Hash_Map<T>* map) {
    for (u32 index = 0; index < map->total_buckets; index++) {
        if (map->buckets[index].contents) {
            free(map->buckets[index].contents);
        }
    }

    free(map->buckets);
}

template <typename T>
void hash_map_put(Hash_Map<T>* map, T value, u32 hash) {
    Hash_Bucket<T>* target_bucket = &map->buckets[hash % map->total_buckets];

    const bool children_not_allocated = !target_bucket->contents;

    if (children_not_allocated || target_bucket->current_size == target_bucket->size_limit) {
        if (children_not_allocated) {
            target_bucket->size_limit = 4;
        } else {
            target_bucket->size_limit *= 2;
        }

        target_bucket->contents = (T*) realloc(target_bucket->contents, sizeof(T) * target_bucket->size_limit);
    }

    target_bucket->contents[target_bucket->current_size++] = value;

    map->size++;
}

template <typename T>
T hash_map_get(Hash_Map<T>* map, String &key, u32 hash) {
    Hash_Bucket<T>* target_bucket = &map->buckets[hash % map->total_buckets];

    for (u32 i = 0; i < target_bucket->current_size; i++) {
        T bucket_element = target_bucket->contents[i];

        if (hash == bucket_element->id_hash && are_strings_equal(key, bucket_element->id)) {
            return bucket_element;
        }
    }

    return NULL;
}
