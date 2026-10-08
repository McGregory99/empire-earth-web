/*
 * Network adapters: enumeration for the engine's settings screen and the
 * mapping from an adapter number to one of the host's IPv4 addresses.
 */
#ifndef TCP_PROTOCOL_ADAPTERS_H
#define TCP_PROTOCOL_ADAPTERS_H

/*
 * Position, in the host's address list (gethostbyname of its own name), of the
 * address of adapter number `adapter` (1-based; 0 means "the default adapter" and
 * yields position 0).  Remembers both numbers in the plugin state.
 */
unsigned long adapter_select(unsigned long adapter);

#endif /* TCP_PROTOCOL_ADAPTERS_H */
