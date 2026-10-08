#include "discovery.h"

#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#include "cipher.h"
#include "exports.h"
#include "plugin_state.h"
#include "protocol.h"

#define SOURCE_FILE "C:\\SSSI Development\\Low-Level Engine\\Network Engine\\TCP Protocol\\TCP Protocol.cpp"
#define SOURCE_LINE_FINISHED_EVENT 0x882
#define SOURCE_LINE_EXIT_EVENT 0x883
#define SOURCE_LINE_THREAD 0x887

/* Floating-point state the searcher thread sets (the engine's single precision, default rounding). */
#define SEARCHER_FP_PRECISION 0x10000u
#define SEARCHER_FP_ROUNDING 0u
#define SEARCHER_POLL_MS 50u

/* Copy a NUL-terminated name into a protocol field, truncating to 64 characters. */
static void put_name(uint8_t *field, const char *name)
{
    size_t length = strlen(name);

    if (length > DISCOVERY_NAME_FIELD - 1) {
        length = DISCOVERY_NAME_FIELD - 1;
    }
    memcpy(field, name, length);
    field[length] = '\0';
}

/* Read a name from a protocol field, never past the field. */
static void get_name(char *text, const uint8_t *field)
{
    memcpy(text, field, DISCOVERY_NAME_FIELD);
    text[DISCOVERY_NAME_FIELD - 1] = '\0';
}

/* Sockets of one scan: an unbound one that broadcasts the probe, one on port 33334 that hears the replies. */
static bool open_scan_sockets(SOCKET *probe_socket, SOCKET *reply_socket)
{
    BOOL broadcast = TRUE;
    u_long nonblocking = 1;
    struct sockaddr_in any;

    *probe_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*probe_socket == INVALID_SOCKET ||
        setsockopt(*probe_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof broadcast) != 0) {
        report_socket_error_and_acknowledge();
        return false;
    }

    memset(&any, 0, sizeof any);
    any.sin_family = AF_INET;
    any.sin_port = htons(GAME_PORT);
    *reply_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (bind(*reply_socket, (struct sockaddr *)&any, sizeof any) != 0 ||
        ioctlsocket(*reply_socket, FIONBIO, &nonblocking) != 0) {
        report_socket_error_and_acknowledge();
        return false;
    }
    return true;
}

static bool send_probe(SOCKET probe_socket, const UString *service)
{
    uint8_t probe[DISCOVERY_PROBE_SIZE] = { 0 };
    struct sockaddr_in broadcast;

    probe[PACKET_HEADER_SIZE] = PACKET_DISCOVERY_PROBE;
    put_name(probe + PACKET_HEADER_SIZE + 1, service->chars);
    game_list_clear(&g_plugin.scan_results);
    datagram_seal(probe, sizeof probe);

    memset(&broadcast, 0, sizeof broadcast);
    broadcast.sin_family = AF_INET;
    broadcast.sin_port = htons(GAME_PORT);
    broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    if (sendto(probe_socket, (const char *)probe, sizeof probe, 0, (struct sockaddr *)&broadcast,
               sizeof broadcast) == SOCKET_ERROR) {
        report_socket_error_and_acknowledge();
        return false;
    }
    return true;
}

/* Remember the game that sent `reply`, unless that host has already answered during this scan. */
static void record_reply(const uint8_t *reply, const struct sockaddr_in *from)
{
    char text[DISCOVERY_NAME_FIELD];
    DetectedGame *game = detected_game_create();

    if (!game) {
        return;
    }
    ustring_assign_chars(&game->host, inet_ntoa(from->sin_addr));
    if (game_list_find_host(&g_plugin.scan_results, &game->host) >= 0) {
        detected_game_destroy(game);
        return;
    }
    get_name(text, reply + DISCOVERY_REPLY_NAME_OFFSET);
    uwidestring_assign_chars(&game->name, text);
    get_name(text, reply + DISCOVERY_REPLY_SERVICE_OFFSET);
    ustring_assign_chars(&game->service, text);
    game->udp_address = *from;
    if (!game_list_append(&g_plugin.scan_results, game)) {
        detected_game_destroy(game);
    }
}

static void collect_replies(SOCKET reply_socket)
{
    unsigned long deadline = tm_get_time() + DISCOVERY_LISTEN_MS;

    while (tm_get_time() < deadline) {
        uint8_t reply[0x200];
        struct sockaddr_in from;
        int from_length = sizeof from;
        int length = recvfrom(reply_socket, (char *)reply, sizeof reply, 0, (struct sockaddr *)&from, &from_length);

        if (length > 0) {
            datagram_open(reply, length);
        }
        if (length == DISCOVERY_REPLY_SIZE && reply[PACKET_HEADER_SIZE] == PACKET_DISCOVERY_REPLY) {
            record_reply(reply, &from);
        }
        Sleep(1);
    }
}

long discovery_scan(const UString *service)
{
    SOCKET probe_socket = INVALID_SOCKET, reply_socket = INVALID_SOCKET;
    long result = NET_FAILED;

    if (service->length == 0) {
        return NET_OK;
    }
    lock_enter(g_plugin.scan_lock);
    if (open_scan_sockets(&probe_socket, &reply_socket) && send_probe(probe_socket, service)) {
        collect_replies(reply_socket);
        lock_enter(g_plugin.game_lock);
        game_list_assign(&g_plugin.detected_games, &g_plugin.scan_results);
        lock_leave(g_plugin.game_lock);
        result = NET_OK;
    }
    if (reply_socket != INVALID_SOCKET) {
        closesocket(reply_socket);
    }
    if (probe_socket != INVALID_SOCKET) {
        closesocket(probe_socket);
    }
    lock_leave(g_plugin.scan_lock);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Background search                                                         */
/* ------------------------------------------------------------------------ */

/* Thread body: scan every 50 ms until the exit event is signalled. */
static unsigned long searcher_main(void *argument)
{
    UString *request = argument;
    UString service;
    HANDLE exit_handle = ts_event_handle(g_plugin.searcher.exit_event);
    bool done = false;

    ustring_construct(&service);
    ustring_assign(&service, request);
    ustring_destruct(request);
    free(request);

    um_set_floating_point_precision(SEARCHER_FP_PRECISION);
    while (!done) {
        if (um_get_floating_point_precision() != SEARCHER_FP_PRECISION) {
            um_set_floating_point_precision(SEARCHER_FP_PRECISION);
        }
        if (um_get_floating_point_rounding_mode() != SEARCHER_FP_ROUNDING) {
            um_set_floating_point_rounding_mode(SEARCHER_FP_ROUNDING);
        }
        switch (WaitForSingleObject(exit_handle, SEARCHER_POLL_MS)) {
        case WAIT_OBJECT_0:
            tsevent_clear(g_plugin.searcher.exit_event);
            done = true;
            break;
        case WAIT_TIMEOUT:
            discovery_scan(&service);
            break;
        case WAIT_FAILED:
            GetLastError();
            break;
        default:
            break;
        }
    }
    tsevent_set(g_plugin.searcher.finished_event);
    ustring_destruct(&service);
    return 0;
}

static void start_search(const UString *service)
{
    UString *request;

    if (g_plugin.searcher.running) {
        return;
    }
    game_list_clear(&g_plugin.detected_games);
    if (tsscheduler_create_event(&g_plugin.searcher.finished_event, true) != 0) {
        es_verify(SOURCE_FILE, SOURCE_LINE_FINISHED_EVENT,
                  "TSScheduler::CreateEvent(gSearcherThreadFinishedEvent) == keseOK");
    }
    if (tsscheduler_create_event(&g_plugin.searcher.exit_event, true) != 0) {
        es_verify(SOURCE_FILE, SOURCE_LINE_EXIT_EVENT, "TSScheduler::CreateEvent(gSearcherThreadExitEvent) == keseOK");
    }
    request = malloc(sizeof *request);
    if (request) {
        ustring_construct_copy(request, service);
    }
    if (tsscheduler_create_thread(&g_plugin.searcher.thread, searcher_main, request, 0, false) != 0) {
        es_verify(SOURCE_FILE, SOURCE_LINE_THREAD,
                  "TSScheduler::CreateThread(gSearcherThread, Searcher, (void*)theService) == keseOK");
    }
    g_plugin.searcher.running = true;
}

static void stop_search(void)
{
    if (g_plugin.searcher.running) {
        tsevent_set(g_plugin.searcher.exit_event);
        tsevent_wait(g_plugin.searcher.finished_event, INFINITE);
        tsscheduler_destroy_thread(g_plugin.searcher.thread);
        tsscheduler_destroy_event(g_plugin.searcher.finished_event);
        tsscheduler_destroy_event(g_plugin.searcher.exit_event);
        g_plugin.searcher.running = false;
    }
    game_list_clear(&g_plugin.detected_games);
}

long NPIMSearchForGame(const UString *unused, const UString *service, unsigned char start)
{
    (void)unused;
    if (service->length != 0) {
        if (start == 1) {
            start_search(service);
        } else {
            stop_search();
        }
    }
    return NET_OK;
}

/* ------------------------------------------------------------------------ */
/* The list the engine reads                                                 */
/* ------------------------------------------------------------------------ */

unsigned long NPIMGetDetectedGameCount(void)
{
    unsigned long count;

    lock_enter(g_plugin.game_lock);
    count = (unsigned long)g_plugin.detected_games.count;
    lock_leave(g_plugin.game_lock);
    return count;
}

long NPIMGetDetectedGame(unsigned long index, UString *host, UString *service, UWideString *name)
{
    long result = NET_OUT_OF_RANGE;

    lock_enter(g_plugin.game_lock);
    if (index < g_plugin.detected_games.count) {
        const DetectedGame *game = g_plugin.detected_games.items[index];

        ustring_assign(host, &game->host);
        ustring_assign(service, &game->service);
        uwidestring_assign(name, &game->name);
        result = NET_OK;
    }
    lock_leave(g_plugin.game_lock);
    return result;
}
