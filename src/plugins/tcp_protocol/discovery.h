/*
 * Finding games on the LAN.
 *
 * A scan broadcasts a "probe" datagram naming a service to UDP port 33334 and
 * listens for one second for "reply" datagrams (see protocol.h).  Every host
 * that answers becomes a DetectedGame.  Scans run on demand (when publishing,
 * to avoid duplicate game names) and continuously in a background thread once
 * NPIMSearchForGame was called.
 */
#ifndef TCP_PROTOCOL_DISCOVERY_H
#define TCP_PROTOCOL_DISCOVERY_H

#include "low_level_engine.h"

/*
 * Run one scan for `service` and replace the list of detected games with the
 * result.  Returns NET_OK, or NET_FAILED when the sockets could not be set up
 * or the probe could not be sent.  An empty service name scans nothing.
 */
long discovery_scan(const UString *service);

#endif /* TCP_PROTOCOL_DISCOVERY_H */
