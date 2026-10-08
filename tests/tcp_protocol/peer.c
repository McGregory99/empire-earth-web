/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

/* ------------------------------------------------------------------------ */
/* Raw Winsock peer                                                          */
/* ------------------------------------------------------------------------ */

struct sockaddr_in make_addr(const char *ip, unsigned port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = ip ? inet_addr(ip) : htonl(INADDR_ANY);
    return addr;
}

/* UDP socket, optionally bound.  `reuse` lets us share a port with the plugin. */
SOCKET udp_socket(const char *ip, unsigned port, int reuse)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        return s;
    }
    if (reuse) {
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
    }
    if (ip || port) {
        struct sockaddr_in addr = make_addr(ip, port);
        if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
    return s;
}

int wait_readable(SOCKET s, int timeout_ms)
{
    fd_set set;
    struct timeval tv;
    FD_ZERO(&set);
    FD_SET(s, &set);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(0, &set, NULL, NULL, &tv) > 0;
}

int udp_receive(SOCKET s, uint8_t *buffer, int capacity, struct sockaddr_in *from, int timeout_ms)
{
    int from_length = sizeof *from;
    if (!wait_readable(s, timeout_ms)) {
        return -1;
    }
    return recvfrom(s, (char *)buffer, capacity, 0, (struct sockaddr *)from, &from_length);
}

int udp_send(SOCKET s, const void *data, int length, const struct sockaddr_in *to)
{
    return sendto(s, (const char *)data, length, 0, (const struct sockaddr *)to, sizeof *to);
}

SOCKET tcp_listener(const char *ip, unsigned port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = make_addr(ip, port);
    if (s == INVALID_SOCKET) {
        return s;
    }
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

SOCKET tcp_accept_peer(SOCKET listener, int timeout_ms)
{
    if (!wait_readable(listener, timeout_ms)) {
        return INVALID_SOCKET;
    }
    return accept(listener, NULL, NULL);
}

SOCKET tcp_connect_peer(const char *ip, unsigned port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = make_addr(ip, port);
    if (s == INVALID_SOCKET) {
        return s;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

int tcp_read(SOCKET s, uint8_t *buffer, int capacity, int timeout_ms)
{
    if (!wait_readable(s, timeout_ms)) {
        return -1;
    }
    return recv(s, (char *)buffer, capacity, 0);
}

