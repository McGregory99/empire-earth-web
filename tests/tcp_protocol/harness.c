/*
 * Differential test harness for Empire Earth's network plugin "TCP Protocol.dll".
 *
 * Usage: harness.exe <plugin.dll> [scenario ...]
 *
 * The harness loads the given plugin the way the engine does (LoadLibraryEx
 * with an altered search path, then GetProcAddress by name), calls its exports
 * with the same argument conventions as the engine's NEProtocol class, and
 * prints a normalized transcript to stdout: one line per call (or group of
 * related observations) with inputs, return codes and outputs.
 *
 * A raw Winsock "peer" lives inside the harness.  It records the exact bytes
 * the plugin puts on the wire and injects crafted datagrams (including the
 * encrypted game-discovery replies), so wire compatibility is part of the
 * transcript.  Values that legitimately differ from run to run (ephemeral
 * ports, the random header byte of encrypted datagrams, uninitialised padding,
 * local addresses) are normalized.
 *
 * The transcript produced for the ORIGINAL DLL is the golden file; a
 * reimplementation passes when it produces the identical transcript.
 */
#include <fcntl.h>
#include <io.h>

#include "harness.h"

static const struct Scenario {
    const char *name;
    void (*run)(void);
} kScenarios[] = {
    { "exports", scenario_exports },
    { "identity", scenario_identity },
    { "activation", scenario_activation },
    { "configuration", scenario_configuration },
    { "network", scenario_network_info },
    { "adapters", scenario_adapters },
    { "detected-empty", scenario_empty_detected_games },
    { "send-single", scenario_send_single },
    { "udp-client", scenario_udp_client_wire },
    { "tcp-client", scenario_tcp_client },
    { "single-transport", scenario_single_transport_servers },
    { "udp-server", scenario_udp_server },
    { "probe-flags", scenario_probe_flags },
    { "discovery", scenario_discovery },
    { "close-order", scenario_close_order_quirk },
};

/* Optional extra arguments restrict the run to the named scenarios (debugging aid). */
static int scenario_selected(const char *name, int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static DWORD WINAPI watchdog_main(LPVOID argument)
{
    (void)argument;
    Sleep(180000);
    line("WATCHDOG: harness took too long, aborting");
    ExitProcess(3);
    return 0;
}

int main(int argc, char **argv)
{
    WSADATA wsa;

    _setmode(_fileno(stdout), _O_BINARY);
    if (argc < 2) {
        fprintf(stderr, "usage: harness <path to TCP Protocol.dll>\n");
        return 2;
    }
    CreateThread(NULL, 0, watchdog_main, NULL, 0, NULL);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    if (!LoadLibraryA("Low-Level Engine.dll") || !bind_plugin(argv[1])) {
        line("FATAL: cannot load plugin %s (error %lu)", argv[1], GetLastError());
        return 2;
    }
    init_key_table();

    for (size_t i = 0; i < sizeof kScenarios / sizeof kScenarios[0]; i++) {
        if (argc != 2 && !scenario_selected(kScenarios[i].name, argc, argv)) {
            continue;
        }
        if (i > 2 && !g_plugin_active) {
            line("SKIPPED %s: the plugin did not activate", kScenarios[i].name);
            continue;
        }
        kScenarios[i].run();
    }

    line("NPIMDeactivate -> %ld", P.Deactivate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("== done");
    return 0;
}
