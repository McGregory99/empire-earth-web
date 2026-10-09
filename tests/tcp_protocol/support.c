/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

NeHandle *new_handle(void)
{
    NeHandle *handle = calloc(1, sizeof *handle);
    handle->protocol = (void *)(uintptr_t)0x4e455052; /* "NEPR" */
    return handle;
}

TSEvent *new_event(void)
{
    TSEvent *event = NULL;
    tsscheduler_create_event(&event, 1);
    return event;
}

void event_reset(TSEvent *event)
{
    ResetEvent(ts_event_handle(event));
}

const char *event_state(TSEvent *event, int wait_ms)
{
    return WaitForSingleObject(ts_event_handle(event), (DWORD)wait_ms) == WAIT_OBJECT_0 ? "signaled" : "not signaled";
}

/* First non-loopback IPv4 address of this host, or NULL. */
const char *lan_address(void)
{
    static char text[32];
    char host[256];
    struct hostent *entry;
    if (gethostname(host, sizeof host) != 0 || !(entry = gethostbyname(host))) {
        return NULL;
    }
    for (int i = 0; entry->h_addr_list[i]; i++) {
        const char *candidate = inet_ntoa(*(struct in_addr *)entry->h_addr_list[i]);
        if (strncmp(candidate, "127.", 4) != 0) {
            snprintf(text, sizeof text, "%s", candidate);
            return text;
        }
    }
    return NULL;
}

ServerEvents new_server_events(void)
{
    ServerEvents events = { new_event(), new_event(), new_event() };
    return events;
}

void free_server_events(ServerEvents *events)
{
    tsscheduler_destroy_event(events->activity);
    tsscheduler_destroy_event(events->xfer_data);
    tsscheduler_destroy_event(events->xfer_accept);
}

long publish(NeHandle *server, const char *service_text, const char *name_text, unsigned long protocols,
                    ServerEvents *events, int skip_duplicate_check, int visible)
{
    UString service;
    UWideString name;
    ustring_construct_from_chars(&service, service_text);
    uwidestring_construct_from_chars(&name, name_text);
    long rc = P.Publish(&service, &name, protocols, events->activity, events->xfer_data, events->xfer_accept,
                        server, skip_duplicate_check, visible, 0);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
    return rc;
}


/* ------------------------------------------------------------------------ */
/* A fake game on the LAN                                                    */
/* ------------------------------------------------------------------------ */

/* Keep answering "a game of service S named N lives here" to port 33334 on this host. */
static DWORD WINAPI feeder_main(LPVOID argument)
{
    Feeder *feeder = argument;
    SOCKET s = udp_socket(NULL, 0, 0);
    struct sockaddr_in target = make_addr("127.0.0.1", GAME_PORT);
    while (feeder->running) {
        udp_send(s, feeder->datagram, sizeof feeder->datagram, &target);
        Sleep(60);
    }
    closesocket(s);
    return 0;
}

void feeder_start(Feeder *feeder, const char *service, const char *name)
{
    uint8_t plain[0x87];
    size_t service_length = strlen(service) < 64 ? strlen(service) : 64;
    size_t name_length = strlen(name) < 68 ? strlen(name) : 68;  /* the 69-byte name field holds 68 characters */

    memset(plain, 0, sizeof plain);
    plain[0] = 0x16;
    memcpy(plain + 1, service, service_length);
    memcpy(plain + 0x42, name, name_length);
    seal(feeder->datagram, 0x37, plain, sizeof plain);
    feeder->running = 1;
    feeder->thread = CreateThread(NULL, 0, feeder_main, feeder, 0, NULL);
}

void feeder_stop(Feeder *feeder)
{
    feeder->running = 0;
    WaitForSingleObject(feeder->thread, 3000);
    CloseHandle(feeder->thread);
}

/* Can this host send a UDP broadcast?  (A LAN scan needs it.) */
int broadcast_available(void)
{
    SOCKET s = udp_socket(NULL, 0, 0);
    BOOL on = TRUE;
    struct sockaddr_in to = make_addr("255.255.255.255", 9);
    int ok;
    if (s == INVALID_SOCKET) {
        return 0;
    }
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
    ok = udp_send(s, "x", 1, &to) == 1;
    closesocket(s);
    return ok;
}
