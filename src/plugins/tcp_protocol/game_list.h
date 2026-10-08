/*
 * Lists of games detected on the LAN.
 *
 * The plugin keeps two: the one the engine reads (guarded by the game lock)
 * and a private one that a scan fills while it listens for replies.  At the end
 * of a scan the private list replaces the public one.
 */
#ifndef TCP_PROTOCOL_GAME_LIST_H
#define TCP_PROTOCOL_GAME_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <winsock2.h>

#include "low_level_engine.h"

typedef struct DetectedGame {
    /*
     * Where to reach the game.  Connecting over TCP uses `tcp_address`, UDP uses
     * `udp_address`.  Only the latter is ever filled in (with the sender of the
     * discovery reply); the original therefore connects TCP clients to 0.0.0.0.
     */
    struct sockaddr_in tcp_address;
    struct sockaddr_in udp_address;
    UString host;       /* dotted-quad text of the sender, e.g. "192.168.0.7" */
    UString service;    /* the service name the game advertises */
    UWideString name;   /* the game's display name */
} DetectedGame;

typedef struct GameList {
    DetectedGame **items;
    size_t count;
    size_t capacity;
} GameList;

/* A game with empty strings and zeroed addresses; NULL when out of memory. */
DetectedGame *detected_game_create(void);
DetectedGame *detected_game_clone(const DetectedGame *game);
void detected_game_destroy(DetectedGame *game);

void game_list_init(GameList *list);
void game_list_clear(GameList *list);
void game_list_release(GameList *list);
bool game_list_append(GameList *list, DetectedGame *game);  /* takes ownership on success */
/* Replace the contents of `list` with copies of the games in `source`. */
void game_list_assign(GameList *list, const GameList *source);
/* Index of the game with this host text, or -1. */
long game_list_find_host(const GameList *list, const UString *host);

#endif /* TCP_PROTOCOL_GAME_LIST_H */
