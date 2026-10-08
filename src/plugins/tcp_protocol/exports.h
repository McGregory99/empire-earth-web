/*
 * The 33 functions "TCP Protocol.dll" exports.
 *
 * The engine (class NEProtocol) looks them up by name with GetProcAddress and
 * calls them through function pointers using the plain cdecl convention.  The
 * NEServer / NEConnection objects are 8-byte wrappers allocated by the engine;
 * the plugin only ever touches their first word, where it keeps a pointer to
 * its own state, and which it clears when the connection or server is closed.
 *
 * Return values of type long are NetStatus codes (see protocol.h).
 */
#ifndef TCP_PROTOCOL_EXPORTS_H
#define TCP_PROTOCOL_EXPORTS_H

#include <stdint.h>

#include "low_level_engine.h"

struct ServerState;
struct ConnectionState;

typedef struct NEServer {
    struct ServerState *state;
    void *protocol;  /* owning NEProtocol, unused by the plugin */
} NEServer;

typedef struct NEConnection {
    struct ConnectionState *state;
    void *protocol;
} NEConnection;

/* Template of the std::vector<UWideString> (MSVC 6 layout) the engine passes to NPIMEnumerateNetworkAdapters. */
typedef struct WideStringVector {
    uint32_t allocator_padding;
    UWideString *first;
    UWideString *last;
    UWideString *end_of_storage;
} WideStringVector;

ENGINE_ABI_CHECK(wide_string_vector_is_16_bytes, sizeof(WideStringVector) == 16);

/* --- module identity and life cycle (module.c) ---------------------------- */
UString *PIMGetName(void);
unsigned long PIMGetType(void);
unsigned char PIMIsValid(void);
unsigned long NPIMGetProtocolType(void);
long NPIMActivate(void);
long NPIMDeactivate(void);
long NPIMGetStatus(const UWideString *status, unsigned long port);
long NPIMGetConfiguration(NEProtocolConfiguration **configuration);
long NPIMSetConfiguration(const NEProtocolConfiguration *configuration);

/* --- network adapters (adapters.c) ------------------------------------------ */
unsigned long NPIMEnumerateNetworkAdapters(WideStringVector *adapters);
UWideString *NPIMGetLocalAddress(unsigned long adapter);

/* --- games found on the LAN (game_list.c / discovery.c) -------------------- */
unsigned long NPIMGetDetectedGameCount(void);
long NPIMGetDetectedGame(unsigned long index, UString *host, UString *service, UWideString *name);
long NPIMSearchForGame(const UString *unused, const UString *service, unsigned char start);

/* --- hosting a game (server.c) ----------------------------------------------- */
long NPIMPublish(const UString *service, const UWideString *name, unsigned long transports,
                 TSEvent *socket_event, TSEvent *transfer_data_event, TSEvent *transfer_accept_event,
                 NEServer *server, unsigned char internet_game, unsigned char announce_on_lan,
                 unsigned long adapter);
long NPIMCloseServer(NEServer *server);
void NPIMAllowNewConnections(NEServer *server, unsigned char allow);
long NPIMAccept(const NEServer *server, NEConnection *connection);
long NPIMReceiveFrom(const NEServer *server, unsigned long capacity, uint8_t *buffer,
                     unsigned long *received, unsigned long *client);
long NPIMSendTo(const NEServer *server, unsigned long client, uint8_t *buffer, unsigned long length);
long NPIMGetClientAddress(const NEServer *server, unsigned long client, UWideString *address);
void NPIMResetClientConnection(const NEServer *server, unsigned long client);
long NPIMAcceptForXfer(const NEServer *server, NEConnection *connection);
long NPIMXferSendTo(const NEServer *server, unsigned long client, const uint8_t *buffer, unsigned long length);
void NPIMResetXfer(const NEServer *server);

/* --- joining a game (connection.c) ------------------------------------------- */
long NPIMConnect(const UString *address, const UString *service, const UWideString *name,
                 unsigned long transport, TSEvent *event, NEConnection *connection, unsigned long adapter);
long NPIMCloseConnection(NEConnection *connection);
long NPIMSend(const NEConnection *connection, uint8_t *buffer, unsigned long length);
long NPIMReceive(const NEConnection *connection, unsigned long capacity, uint8_t *buffer,
                 unsigned long *received);
unsigned char NPIMIsAlive(const NEConnection *connection);
long NPIMSendSingle(const UString *address, unsigned short port, const uint8_t *buffer, unsigned long length);
long NPIMConnectForXfer(const NEConnection *server_connection, unsigned long transport, TSEvent *event,
                        NEConnection *connection);
long NPIMXferReceive(unsigned long capacity, uint8_t *buffer, unsigned long *received);

#endif /* TCP_PROTOCOL_EXPORTS_H */
