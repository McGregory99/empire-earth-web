/*
 * A growable array of pointers: the plugin's replacement for the
 * std::vector<T*> of the original (client addresses, transfer connections).
 *
 * It reproduces two properties of the original container that the plugin's
 * behavior depends on:
 *   - the capacity grows by one half (at least to fit the new element), and
 *   - clearing the array keeps its storage, so a slot beyond size() can still
 *     be written with ptr_array_store() (NPIMAcceptForXfer does this after
 *     NPIMResetXfer).
 */
#ifndef TCP_PROTOCOL_PTR_ARRAY_H
#define TCP_PROTOCOL_PTR_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct PtrArray {
    void **items;
    size_t size;
    size_t capacity;
} PtrArray;

void ptr_array_init(PtrArray *array);
/* Release the storage (the pointed-to objects are not touched). */
void ptr_array_release(PtrArray *array);
/* Forget all elements but keep the storage. */
void ptr_array_clear(PtrArray *array);
/* Append; returns false when out of memory. */
bool ptr_array_push(PtrArray *array, void *item);
/*
 * Store `item` at `index` without changing the size, even if index >= size
 * (the slot is then spare capacity).  Returns false when out of memory.
 */
bool ptr_array_store(PtrArray *array, size_t index, void *item);

#endif /* TCP_PROTOCOL_PTR_ARRAY_H */
