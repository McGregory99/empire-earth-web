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
#include <process.h>

#include "harness.h"

static const struct Scenario {
    const char *name;
    void (*run)(void);
    int isolated; /* run in a process of its own, after the others */
} kScenarios[] = {
    { "exports", scenario_exports, 0 },
    { "identity", scenario_identity, 0 },
    { "activation", scenario_activation, 0 },
    { "configuration", scenario_configuration, 0 },
    { "network", scenario_network_info, 0 },
    { "adapters", scenario_adapters, 0 },
    { "detected-empty", scenario_empty_detected_games, 0 },
    { "send-single", scenario_send_single, 0 },
    { "udp-client", scenario_udp_client_wire, 0 },
    { "tcp-client", scenario_tcp_client, 0 },
    { "single-transport", scenario_single_transport_servers, 0 },
    { "udp-server", scenario_udp_server, 0 },
    { "probe-flags", scenario_probe_flags, 0 },
    { "discovery", scenario_discovery, 0 },
    { "close-order", scenario_close_order_quirk, 0 },
    { "leak-scan-sendto", scenario_leak_scan_sendto, 1 },
    { "leak-scan-ioctl", scenario_leak_scan_ioctl, 1 },
    { "leak-transfer-listener", scenario_leak_transfer_listener, 1 },
    { "faults", scenario_faults, 0 },
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
    Sleep(240000);
    line("WATCHDOG: harness took too long, aborting");
    ExitProcess(3);
    return 0;
}

/* Run one scenario in a fresh process: `harness <plugin> --isolated <scenario>`; its output joins ours. */
static void run_isolated(const char *plugin, const char *scenario)
{
    char exe[MAX_PATH], quoted_exe[MAX_PATH + 2], quoted_plugin[MAX_PATH + 2];
    intptr_t status;

    GetModuleFileNameA(NULL, exe, sizeof exe);
    snprintf(quoted_exe, sizeof quoted_exe, "\"%s\"", exe);          /* spawn does not quote its arguments */
    snprintf(quoted_plugin, sizeof quoted_plugin, "\"%s\"", plugin);
    fflush(stdout);
    status = _spawnl(_P_WAIT, exe, quoted_exe, quoted_plugin, "--isolated", scenario, (char *)NULL);
    if (status != 0) {
        line("ISOLATED RUN OF %s FAILED (status %ld)", scenario, (long)status);
    }
}

int main(int argc, char **argv)
{
    WSADATA wsa;
    int isolated_child = argc == 4 && strcmp(argv[2], "--isolated") == 0;

    _setmode(_fileno(stdout), _O_BINARY);
    if (argc < 2) {
        fprintf(stderr, "usage: harness <path to TCP Protocol.dll> [scenario ...]\n");
        return 2;
    }
    CreateThread(NULL, 0, watchdog_main, NULL, 0, NULL);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    if (!LoadLibraryA("Low-Level Engine.dll") || !bind_plugin(argv[1])) {
        line("FATAL: cannot load plugin %s (error %lu)", argv[1], GetLastError());
        return 2;
    }
    init_key_table();

    if (isolated_child) {
        /* Activate quietly, then run the one scenario that was asked for. */
        g_plugin_active = P.Activate() == 0;
        for (size_t i = 0; i < sizeof kScenarios / sizeof kScenarios[0]; i++) {
            if (strcmp(kScenarios[i].name, argv[3]) == 0 && g_plugin_active) {
                kScenarios[i].run();
            }
        }
        return 0;
    }

    for (size_t i = 0; i < sizeof kScenarios / sizeof kScenarios[0]; i++) {
        if (argc != 2 && !scenario_selected(kScenarios[i].name, argc, argv)) {
            continue;
        }
        if (i > 2 && !g_plugin_active) {
            line("SKIPPED %s: the plugin did not activate", kScenarios[i].name);
        } else if (kScenarios[i].isolated) {
            run_isolated(argv[1], kScenarios[i].name);
        } else {
            kScenarios[i].run();
        }
    }

    line("NPIMDeactivate -> %ld", P.Deactivate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("== done");
    return 0;
}
