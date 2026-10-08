#include "game_list.h"

#include <stdlib.h>
#include <string.h>

/* The engine strings of a game are created with room for 64 characters. */
#define NAME_CAPACITY 0x40

DetectedGame *detected_game_create(void)
{
    DetectedGame *game = calloc(1, sizeof *game);

    if (game) {
        ustring_construct_with_capacity(&game->host, NAME_CAPACITY);
        ustring_construct_with_capacity(&game->service, NAME_CAPACITY);
        uwidestring_construct_with_capacity(&game->name, NAME_CAPACITY);
    }
    return game;
}

DetectedGame *detected_game_clone(const DetectedGame *game)
{
    DetectedGame *copy = malloc(sizeof *copy);

    if (copy) {
        copy->tcp_address = game->tcp_address;
        copy->udp_address = game->udp_address;
        ustring_construct_copy(&copy->host, &game->host);
        ustring_construct_copy(&copy->service, &game->service);
        uwidestring_construct_copy(&copy->name, &game->name);
    }
    return copy;
}

void detected_game_destroy(DetectedGame *game)
{
    if (game) {
        uwidestring_destruct(&game->name);
        ustring_destruct(&game->service);
        ustring_destruct(&game->host);
        free(game);
    }
}

void game_list_init(GameList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void game_list_clear(GameList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        detected_game_destroy(list->items[i]);
    }
    list->count = 0;
}

void game_list_release(GameList *list)
{
    game_list_clear(list);
    free(list->items);
    game_list_init(list);
}

bool game_list_append(GameList *list, DetectedGame *game)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity + list->capacity / 2 + 1 : 4;
        DetectedGame **items = realloc(list->items, capacity * sizeof *items);

        if (!items) {
            return false;
        }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = game;
    return true;
}

void game_list_assign(GameList *list, const GameList *source)
{
    game_list_clear(list);
    for (size_t i = 0; i < source->count; i++) {
        DetectedGame *copy = detected_game_clone(source->items[i]);

        if (copy && !game_list_append(list, copy)) {
            detected_game_destroy(copy);
        }
    }
}

long game_list_find_host(const GameList *list, const UString *host)
{
    for (size_t i = 0; i < list->count; i++) {
        if (ustring_equal(host, &list->items[i]->host)) {
            return (long)i;
        }
    }
    return -1;
}
