#include "discovery.h"

#include <string.h>

#include "cipher.h"
#include "exports.h"
#include "packet.h"
#include "plugin_state.h"
#include "protocol.h"

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

static bool broadcast_probe(SOCKET probe_socket, const UString *service)
{
    uint8_t probe[DISCOVERY_PROBE_SIZE];
    struct sockaddr_in broadcast;

    packet_fill_probe(probe, service->chars);
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
    char service[DISCOVERY_SERVICE_FIELD], game_name[DISCOVERY_GAME_NAME_FIELD];
    DetectedGame *game = detected_game_create();

    if (!game) {
        return;
    }
    ustring_assign_chars(&game->host, inet_ntoa(from->sin_addr));
    if (game_list_find_host(&g_plugin.scan_results, &game->host) >= 0) {
        detected_game_destroy(game);
        return;
    }
    packet_read_reply(reply, service, game_name);
    uwidestring_assign_chars(&game->name, game_name);
    ustring_assign_chars(&game->service, service);
    game->udp_address = *from;
    if (!game_list_append(&g_plugin.scan_results, game)) {
        detected_game_destroy(game);
    }
}

/* Listen for replies for DISCOVERY_LISTEN_MS, polling the non-blocking socket. */
static void collect_replies(SOCKET reply_socket)
{
    unsigned long deadline = tm_get_time() + DISCOVERY_LISTEN_MS;

    while (tm_get_time() < deadline) {
        uint8_t datagram[0x200];
        struct sockaddr_in from;
        int from_length = sizeof from;
        int length = recvfrom(reply_socket, (char *)datagram, sizeof datagram, 0, (struct sockaddr *)&from,
                              &from_length);

        if (length > 0) {
            datagram_open(datagram, length);
        }
        if (packet_is_reply(datagram, length)) {
            record_reply(datagram, &from);
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
    if (open_scan_sockets(&probe_socket, &reply_socket) && broadcast_probe(probe_socket, service)) {
        collect_replies(reply_socket);
        lock_enter(g_plugin.game_lock);
        game_list_assign(&g_plugin.detected_games, &g_plugin.scan_results);
        lock_leave(g_plugin.game_lock);
        closesocket(reply_socket);
        result = NET_OK;
    }
    /*
     * Known defect of the original, kept so that behavior stays identical: a scan that fails
     * never closes its reply socket.  If it got as far as binding it, the socket stays bound to
     * the game port until the process ends, so every later scan and every later NPIMPublish
     * fails with "address in use".
     */
    if (probe_socket != INVALID_SOCKET) {
        closesocket(probe_socket);
    }
    lock_leave(g_plugin.scan_lock);
    return result;
}
