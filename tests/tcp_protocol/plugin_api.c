/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

struct PluginApi P;
HMODULE g_module;

/* Exports in ordinal order (the original numbers them 1..33 alphabetically). */
const char *const kExportNames[33] = {
    "NPIMAccept", "NPIMAcceptForXfer", "NPIMActivate", "NPIMAllowNewConnections",
    "NPIMCloseConnection", "NPIMCloseServer", "NPIMConnect", "NPIMConnectForXfer",
    "NPIMDeactivate", "NPIMEnumerateNetworkAdapters", "NPIMGetClientAddress",
    "NPIMGetConfiguration", "NPIMGetDetectedGame", "NPIMGetDetectedGameCount",
    "NPIMGetLocalAddress", "NPIMGetProtocolType", "NPIMGetStatus", "NPIMIsAlive",
    "NPIMPublish", "NPIMReceive", "NPIMReceiveFrom", "NPIMResetClientConnection",
    "NPIMResetXfer", "NPIMSearchForGame", "NPIMSend", "NPIMSendSingle", "NPIMSendTo",
    "NPIMSetConfiguration", "NPIMXferReceive", "NPIMXferSendTo", "PIMGetName",
    "PIMGetType", "PIMIsValid",
};


#define BIND(field, name)                                                            \
    do {                                                                             \
        FARPROC proc_ = GetProcAddress(g_module, name);                              \
        memcpy(&P.field, &proc_, sizeof proc_);                                      \
    } while (0)

int bind_plugin(const char *path)
{
    char full[MAX_PATH];
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) {
        return 0;
    }
    g_module = LoadLibraryExA(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_module) {
        return 0;
    }
    BIND(Accept, "NPIMAccept");
    BIND(AcceptForXfer, "NPIMAcceptForXfer");
    BIND(Activate, "NPIMActivate");
    BIND(AllowNewConnections, "NPIMAllowNewConnections");
    BIND(CloseConnection, "NPIMCloseConnection");
    BIND(CloseServer, "NPIMCloseServer");
    BIND(Connect, "NPIMConnect");
    BIND(ConnectForXfer, "NPIMConnectForXfer");
    BIND(Deactivate, "NPIMDeactivate");
    BIND(EnumerateNetworkAdapters, "NPIMEnumerateNetworkAdapters");
    BIND(GetClientAddress, "NPIMGetClientAddress");
    BIND(GetConfiguration, "NPIMGetConfiguration");
    BIND(GetDetectedGame, "NPIMGetDetectedGame");
    BIND(GetDetectedGameCount, "NPIMGetDetectedGameCount");
    BIND(GetLocalAddress, "NPIMGetLocalAddress");
    BIND(GetProtocolType, "NPIMGetProtocolType");
    BIND(GetStatus, "NPIMGetStatus");
    BIND(IsAlive, "NPIMIsAlive");
    BIND(Publish, "NPIMPublish");
    BIND(Receive, "NPIMReceive");
    BIND(ReceiveFrom, "NPIMReceiveFrom");
    BIND(ResetClientConnection, "NPIMResetClientConnection");
    BIND(ResetXfer, "NPIMResetXfer");
    BIND(SearchForGame, "NPIMSearchForGame");
    BIND(Send, "NPIMSend");
    BIND(SendSingle, "NPIMSendSingle");
    BIND(SendTo, "NPIMSendTo");
    BIND(SetConfiguration, "NPIMSetConfiguration");
    BIND(XferReceive, "NPIMXferReceive");
    BIND(XferSendTo, "NPIMXferSendTo");
    BIND(PIMGetName, "PIMGetName");
    BIND(PIMGetType, "PIMGetType");
    BIND(PIMIsValid, "PIMIsValid");
    interposer_install(g_module);
    return 1;
}

