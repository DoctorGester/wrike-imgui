#pragma once

#include <cstdlib>
#include "common.h"

static const struct {
    u32 max_entries, size, rehash;
} hash_sizes[] = {
        {2,            5,            3},
        {4,            7,            5},
        {8,            13,           11},
        {16,           19,           17},
        {32,           43,           41},
        {64,           73,           71},
        {128,          151,          149},
        {256,          283,          281},
        {512,          571,          569},
        {1024,         1153,         1151},
        {2048,         2269,         2267},
        {4096,         4519,         4517},
        {8192,         9013,         9011},
        {16384,        18043,        18041},
        {32768,        36109,        36107},
        {65536,        72091,        72089},
        {131072,       144409,       144407},
        {262144,       288361,       288359},
        {524288,       576883,       576881},
        {1048576,      1153459,      1153457},
        {2097152,      2307163,      2307161},
        {4194304,      4613893,      4613891},
        {8388608,      9227641,      9227639},
        {16777216,     18455029,     18455027},
        {33554432,     36911011,     36911009},
        {67108864,     73819861,     73819859},
        {134217728,    147639589,    147639587},
        {268435456,    295279081,    295279079},
        {536870912,    590559793,    590559791},
        {1073741824,   1181116273,   1181116271},
        {2147483648ul, 2362232233ul, 2362232231ul}
};

#define hash_table_foreach(ht, entry)                \
    for (entry = id_hash_table_next_entry(ht, (Id_Hash_Entry<Key, T, NULL_VALUE>*) NULL);        \
         entry != NULL;                    \
         entry = id_hash_table_next_entry(ht, entry))

template<typename Key, typename T, T NULL_VALUE = nullptr>
struct Id_Hash_Entry {
    u32 hash;
    T data;
    Key key;
    bool present;
};

template<typename Key, typename T, T NULL_VALUE = nullptr>
struct Id_Hash_Map {
    Id_Hash_Entry<Key, T, NULL_VALUE>* table = NULL;
    u32 size; // TODO rename this
    u32 rehash;
    u32 max_entries;
    u32 size_index;
    u32 entries;
};

template<typename Key, typename T, T NULL_VALUE = nullptr>
void id_hash_map_init(Id_Hash_Map<Key, T, NULL_VALUE>* map) {
    map->size_index = 0;
    map->size = hash_sizes[map->size_index].size;
    map->rehash = hash_sizes[map->size_index].rehash;
    map->max_entries = hash_sizes[map->size_index].max_entries;
    map->table = (Id_Hash_Entry<Key, T, NULL_VALUE>*) CALLOC(map->size, sizeof(Id_Hash_Entry<Key, T, NULL_VALUE>));
    map->entries = 0;
}

template<typename Key, typename T, T NULL_VALUE = nullptr>
void id_hash_map_destroy(Id_Hash_Map<Key, T, NULL_VALUE>* map) {
    FREE(map->table);
}

template<typename Key, typename T, T NULL_VALUE = nullptr>
void id_hash_map_clear(Id_Hash_Map<Key, T, NULL_VALUE>* map) {
//    if (!map->size) {
//        return;
//    }
//
//    for (u32 index = 0; index < map->total_buckets; index++) {
//        if (map->buckets[index].contents) {
//            FREE(map->buckets[index].contents);
//        }
//    }
//
//    memset(map->buckets, 0, sizeof(Hash_Bucket < T > ) * map->total_buckets);
//    map->size = 0;
}

template<typename Key, typename T, T NULL_VALUE = nullptr>
Id_Hash_Entry<Key, T, NULL_VALUE>* id_hash_table_next_entry(Id_Hash_Map<Key, T, NULL_VALUE>* map, Id_Hash_Entry<Key, T, NULL_VALUE>* entry) {
    if (entry == NULL) {
        entry = map->table;
    } else {
        entry++;
    }

    for (; entry != map->table + map->size; entry++) {
        if (entry->present) {
            return entry;
        }
    }

    return NULL;
}

template<typename Key, typename T, T NULL_VALUE = nullptr>
void id_hash_map_rehash(Id_Hash_Map<Key, T, NULL_VALUE>* map, u32 new_size_index) {
    Id_Hash_Map<Key, T, NULL_VALUE> old_ht;
    Id_Hash_Entry<Key, T, NULL_VALUE>* table;

    if (new_size_index >= ARRAY_SIZE(hash_sizes))
        return;

    table = (Id_Hash_Entry<Key, T, NULL_VALUE>*) CALLOC(hash_sizes[new_size_index].size, sizeof(Id_Hash_Entry<Key, T, NULL_VALUE>));
    if (table == NULL)
        return;

    old_ht = *map;

    map->table = table;
    map->size_index = new_size_index;
    map->size = hash_sizes[map->size_index].size;
    map->rehash = hash_sizes[map->size_index].rehash;
    map->max_entries = hash_sizes[map->size_index].max_entries;
    map->entries = 0;

    Id_Hash_Entry<Key, T, NULL_VALUE>* entry;

    hash_table_foreach(&old_ht, entry) {
        id_hash_map_put(map, entry->data, entry->key, entry->hash);
    }

    FREE(old_ht.table);
}

template<typename Key, typename T, T NULL_VALUE = nullptr>
bool id_hash_map_put(Id_Hash_Map<Key, T, NULL_VALUE>* map, T value, Key key, u32 hash) {
    Id_Hash_Entry<Key, T, NULL_VALUE>* available_entry = NULL;

    if (map->entries >= map->max_entries) {
        id_hash_map_rehash(map, map->size_index + 1);
    }

    u32 start_hash_address = hash % map->size;
    u32 hash_address = start_hash_address;

    do {
        Id_Hash_Entry<Key, T, NULL_VALUE>* entry = map->table + hash_address;
        u32 double_hash;

        if (!entry->present) {
            /* Stash the first available entry we find */
            if (available_entry == NULL)
                available_entry = entry;

            break;
        }

        /* Implement replacement when another insert happens
         * with a matching key.  This is a relatively common
         * feature of hash tables, with the alternative
         * generally being "insert the new value as well, and
         * return it first when the key is searched for".
         *
         * Note that the hash table doesn't have a delete
         * callback.  If freeing of old data pointers is
         * required to avoid memory leaks, perform a search
         * before inserting.
         */
        if (entry->hash == hash && key == entry->key) {
            entry->key = key;
            entry->present = true;
            entry->data = value;
            return true;
        }

        double_hash = 1 + hash % map->rehash;

        hash_address = (hash_address + double_hash) % map->size;
    } while (hash_address != start_hash_address);

    if (available_entry) {
        available_entry->hash = hash;
        available_entry->key = key;
        available_entry->data = value;
        available_entry->present = true;
        map->entries++;
        return true;
    }

    /* We could hit here if a required resize failed. An unchecked-malloc
     * application could ignore this result.
     */
    return false;
}

template<typename Key, typename T, T NULL_VALUE = nullptr>
T id_hash_map_get(Id_Hash_Map<Key, T, NULL_VALUE>* map, Key key, u32 hash) {
    if (!map->size) {
        return NULL_VALUE;
    }

    u32 start_hash_address = hash % map->size;
    u32 hash_address = start_hash_address;

    do {
        u32 double_hash;

        Id_Hash_Entry<Key, T, NULL_VALUE>* entry = map->table + hash_address;

        if (!entry->present) {
            return NULL_VALUE;
        } else if (entry->hash == hash && key == entry->key) {
            return entry->data;
        }

        double_hash = 1 + hash % map->rehash;

        hash_address = (hash_address + double_hash) % map->size;
    } while (hash_address != start_hash_address);

    return NULL_VALUE;
}
