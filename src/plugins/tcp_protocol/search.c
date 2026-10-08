/*
 * The background search for games on the LAN and the list of games it detects.
 */
#include <stdlib.h>

#include "discovery.h"
#include "exports.h"
#include "plugin_state.h"
#include "protocol.h"

#define SOURCE_LINE_FINISHED_EVENT 0x882
#define SOURCE_LINE_EXIT_EVENT 0x883
#define SOURCE_LINE_THREAD 0x887

/* Floating-point state the searcher thread sets (the engine's single precision, default rounding). */
#define SEARCHER_FP_PRECISION 0x10000u
#define SEARCHER_FP_ROUNDING 0u
/* Pause between two scans, which doubles as the reaction time to a stop request. */
#define SEARCHER_PAUSE_MS 50u

/* Thread body: scan, pause, scan, ... until the exit event is signalled. */
static unsigned long searcher_main(void *argument)
{
    UString *request = argument;
    UString service;
    HANDLE exit_handle = ts_event_handle(g_plugin.searcher.exit_event);
    bool done = false;

    /* The thread owns the request; keep a private copy of the service name. */
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
        switch (WaitForSingleObject(exit_handle, SEARCHER_PAUSE_MS)) {
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
        es_verify(ENGINE_ASSERT_FILE, SOURCE_LINE_FINISHED_EVENT,
                  "TSScheduler::CreateEvent(gSearcherThreadFinishedEvent) == keseOK");
    }
    if (tsscheduler_create_event(&g_plugin.searcher.exit_event, true) != 0) {
        es_verify(ENGINE_ASSERT_FILE, SOURCE_LINE_EXIT_EVENT,
                  "TSScheduler::CreateEvent(gSearcherThreadExitEvent) == keseOK");
    }
    request = malloc(sizeof *request);
    if (request) {
        ustring_construct_copy(request, service);
    }
    if (tsscheduler_create_thread(&g_plugin.searcher.thread, searcher_main, request, 0, false) != 0) {
        es_verify(ENGINE_ASSERT_FILE, SOURCE_LINE_THREAD,
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

/* Start (start == 1) or stop (anything else) the continuous search for `service`; an empty name does nothing. */
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
