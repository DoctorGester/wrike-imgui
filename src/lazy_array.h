#include <cstdlib>
#include "common.h"

#pragma once

// TODO looks terribly slow to be honest. Though base might be cached when accessing in a loop
// TODO maybe a lot of MALLOC/free for SubTaskIds/CustomFieldValues is not a bad idea?
// TODO http://floooh.github.io/2018/06/17/handles-vs-pointers.html this is a way better idea
template <typename T>
struct Relative_Pointer {
    T** base;
    u32 offset;

    T& operator *() {
        return *(*base + offset);
    }

    T& operator [](const int index) {
        return *(*base + offset + index);
    }

    T* operator ->() {
        return *base + offset;
    }
};

// TODO an interesting possibility would be to make something like this and allocate on demand
/*
struct Array_Block {
    T elements[1024];
};

struct Block_Array {
    Array_Block* blocks[1024];
};
 */


template <typename T, u8 initial_watermark>
struct Lazy_Array {
    T* data = NULL;
    u32 length = 0;
    u32 watermark = 0;

    T& operator [](const int index) {
        return data[index];
    }
};

template <typename T, u8 initial_watermark>
T* lazy_array_reserve_n_values(Lazy_Array<T, initial_watermark>& array, u32 n) {
//    printf("Reserve %lu, watermark is %lu, length is %lu\n", n, array.watermark, array.length);
    u32 new_length = array.length + n;

    if (new_length > array.watermark) {
//        printf("Will reallocate %p, old wm %lu, old length %lu, new length %lu\n", array.data, array.watermark, array.length, new_length);

        if (array.watermark == 0) {
            array.watermark = MAX(initial_watermark, new_length);
        } else {
            array.watermark = MAX(array.watermark * 2, (u32) (new_length * 1.5));
        }

//        printf("New WM is %lu, new size %lu\n", array.watermark, sizeof(T) * array.watermark);

        array.data = (T*) REALLOC(array.data, sizeof(T) * array.watermark);

//        printf("Reallocated to %p with size %lu\n", array.data, array.watermark);
    }

    T* result = &array.data[array.length];

    array.length = new_length;

    return result;
}

template <typename T, u8 initial_watermark>
Relative_Pointer<T> lazy_array_reserve_n_values_relative_pointer(Lazy_Array<T, initial_watermark>& array, u32 n) {
    Relative_Pointer<T> pointer;

    pointer.base = &array.data;
    pointer.offset = array.length;

    lazy_array_reserve_n_values(array, n);

    return pointer;
};

template <typename T, u8 initial_watermark>
inline void lazy_array_soft_reset(Lazy_Array<T, initial_watermark>& lazy_array) {
    lazy_array.length = 0;
}

template <typename T, u8 initial_watermark>
inline void lazy_array_clear(Lazy_Array<T, initial_watermark>& lazy_array) {
    lazy_array.length = 0;
    lazy_array.watermark = 0;
    FREE(lazy_array.data);
    lazy_array.data = NULL;
}

template <typename T>
Relative_Pointer<T> relative_pointer(T*& base, u16 offset) {
    Relative_Pointer<T> pointer;
    pointer.base = &base;
    pointer.offset = offset;

    return pointer;
}
