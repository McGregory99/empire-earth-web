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

