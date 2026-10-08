#include "ptr_array.h"

#include <stdlib.h>
#include <string.h>

void ptr_array_init(PtrArray *array)
{
    array->items = NULL;
    array->size = 0;
    array->capacity = 0;
}

void ptr_array_release(PtrArray *array)
{
    free(array->items);
    ptr_array_init(array);
}

void ptr_array_clear(PtrArray *array)
{
    array->size = 0;
}

/* Make room for at least `needed` slots; new slots start out NULL. */
static bool ensure_capacity(PtrArray *array, size_t needed)
{
    size_t capacity;
    void **items;

    if (needed <= array->capacity) {
        return true;
    }
    capacity = array->capacity + array->capacity / 2;
    if (capacity < needed) {
        capacity = needed;
    }
    items = realloc(array->items, capacity * sizeof *items);
    if (!items) {
        return false;
    }
    memset(items + array->capacity, 0, (capacity - array->capacity) * sizeof *items);
    array->items = items;
    array->capacity = capacity;
    return true;
}

bool ptr_array_push(PtrArray *array, void *item)
{
    if (!ensure_capacity(array, array->size + 1)) {
        return false;
    }
    array->items[array->size++] = item;
    return true;
}

bool ptr_array_store(PtrArray *array, size_t index, void *item)
{
    if (!ensure_capacity(array, index + 1)) {
        return false;
    }
    array->items[index] = item;
    return true;
}
