/*
 * Deliberately broken "TCP Protocol.dll" used as a negative control.
 *
 * It exports the same 33 symbols as the real plugin but every function just
 * reports failure (or returns zero).  `make test-stub` runs the differential
 * harness against it and expects the comparison with the golden transcript to
 * FAIL, which proves the harness can actually tell a wrong plugin from the
 * original.  It was also the "RED" step of the test-first development of the
 * real reimplementation.
 */
#include <stdint.h>
#include <windows.h>

/* Large enough for an engine UString / UWideString; all zero. */
static uint32_t g_empty_name[8];

#define STATUS_FAILED 1

long NPIMAccept(void *server, void *connection)            { (void)server; (void)connection; return STATUS_FAILED; }
long NPIMAcceptForXfer(void *server, void *connection)     { (void)server; (void)connection; return STATUS_FAILED; }
long NPIMActivate(void)                                    { return STATUS_FAILED; }
void NPIMAllowNewConnections(void *server, int allow)      { (void)server; (void)allow; }
long NPIMCloseConnection(void *connection)                 { (void)connection; return STATUS_FAILED; }
long NPIMCloseServer(void *server)                         { (void)server; return STATUS_FAILED; }
long NPIMConnect(const void *a, const void *s, const void *n, unsigned long p, void *e, void *c, unsigned long ad)
{ (void)a; (void)s; (void)n; (void)p; (void)e; (void)c; (void)ad; return STATUS_FAILED; }
long NPIMConnectForXfer(void *c, unsigned long p, void *e, void *o) { (void)c; (void)p; (void)e; (void)o; return STATUS_FAILED; }
long NPIMDeactivate(void)                                  { return STATUS_FAILED; }
unsigned long NPIMEnumerateNetworkAdapters(void *list)     { (void)list; return 0; }
long NPIMGetClientAddress(void *s, unsigned long i, void *o) { (void)s; (void)i; (void)o; return STATUS_FAILED; }
long NPIMGetConfiguration(void **configuration)            { (void)configuration; return STATUS_FAILED; }
long NPIMGetDetectedGame(unsigned long i, void *a, void *b, void *c) { (void)i; (void)a; (void)b; (void)c; return STATUS_FAILED; }
unsigned long NPIMGetDetectedGameCount(void)               { return 0; }
void *NPIMGetLocalAddress(unsigned long adapter)           { (void)adapter; return g_empty_name; }
unsigned long NPIMGetProtocolType(void)                    { return 0; }
long NPIMGetStatus(const void *status, unsigned long port) { (void)status; (void)port; return STATUS_FAILED; }
unsigned char NPIMIsAlive(void *connection)                { (void)connection; return 0; }
long NPIMPublish(const void *a, const void *b, unsigned long c, void *d, void *e, void *f, void *g, int h, int i, unsigned long j)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; return STATUS_FAILED; }
long NPIMReceive(void *c, unsigned long n, uint8_t *b, unsigned long *r) { (void)c; (void)n; (void)b; (void)r; return STATUS_FAILED; }
long NPIMReceiveFrom(void *s, unsigned long n, uint8_t *b, unsigned long *r, unsigned long *i)
{ (void)s; (void)n; (void)b; (void)r; (void)i; return STATUS_FAILED; }
void NPIMResetClientConnection(void *server, unsigned long i) { (void)server; (void)i; }
void NPIMResetXfer(void *server)                           { (void)server; }
long NPIMSearchForGame(const void *a, const void *b, int start) { (void)a; (void)b; (void)start; return STATUS_FAILED; }
long NPIMSend(void *c, const uint8_t *b, unsigned long n)  { (void)c; (void)b; (void)n; return STATUS_FAILED; }
long NPIMSendSingle(const void *a, unsigned long p, const uint8_t *b, unsigned long n) { (void)a; (void)p; (void)b; (void)n; return STATUS_FAILED; }
long NPIMSendTo(void *s, unsigned long i, const uint8_t *b, unsigned long n) { (void)s; (void)i; (void)b; (void)n; return STATUS_FAILED; }
long NPIMSetConfiguration(void *configuration)             { (void)configuration; return STATUS_FAILED; }
long NPIMXferReceive(unsigned long n, uint8_t *b, unsigned long *r) { (void)n; (void)b; (void)r; return STATUS_FAILED; }
long NPIMXferSendTo(void *s, unsigned long i, const uint8_t *b, unsigned long n) { (void)s; (void)i; (void)b; (void)n; return STATUS_FAILED; }
void *PIMGetName(void)                                     { return g_empty_name; }
unsigned long PIMGetType(void)                             { return 0; }
unsigned char PIMIsValid(void)                             { return 0; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)module; (void)reason; (void)reserved;
    return TRUE;
}
