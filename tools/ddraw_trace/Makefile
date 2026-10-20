# Tracing DirectDraw proxy: an oracle for the Direct3D 7 renderer plugin.
#
#   make              build build/ddraw.dll (the proxy)
#   make test         unit tests (incl. the data file reader): harness + fake plugin under Wine in front of Wine's builtin ddraw
#   make check-tables regenerate src/*.inc from the SDK headers and compare
#   make gen-tables   regenerate src/methods.inc and src/names.inc
#   make game-trace   one in-game capture (takes the game-run lock, launches the game, ~3 min); compares with the previous run
#   make game-gate    two in-game captures and their diff: the determinism gate
#   make game-check   one capture compared with tests/ddraw_trace/golden (this machine's display)
#   make game-gate-skirmish   the paused-skirmish scenario (tests/ddraw_trace/scenarios.md): two captures, identical in structure form
#   make game-check-skirmish  one capture of that scenario compared with its golden
#   make clean
#
# Everything outside the repository (Wine, the test prefix, the game) is configured below and never
# written to the repository.  The proxy is documented in README.md.

TARGET  := i686-w64-mingw32
CC      := $(TARGET)-gcc
# Homebrew's mingw defaults to the UCRT; `msvcrt-os` links the real msvcrt.dll that Wine and the game share.
CRT     := -mcrtdll=msvcrt-os
CFLAGS  ?= -O2
CFLAGS  += -std=gnu99 -Wall -Wextra -Wno-unused-parameter -D_WIN32_WINNT=0x0501 $(CRT)

RE_DIR     ?= $(HOME)/Games/EmpireEarth-re
WINE_ROOT  ?= $(HOME)/Library/Application Support/EmpireEarthMac/runtime/Wine Stable.app/Contents/Resources/wine
WINE       ?= $(WINE_ROOT)/bin/wine
# Always the project's own prefix (":=" so an ambient WINEPREFIX, typically the game's, is never used).
WINEPREFIX := $(RE_DIR)/wineprefix-test
export WINEPREFIX
export WINEDEBUG ?= -all
export MVK_CONFIG_LOG_LEVEL ?= 0

BUILD    := build
TESTBED  := $(BUILD)/testbed
SDK_INC  := $(shell $(CC) -print-sysroot)/$(TARGET)/include
SRC      := $(wildcard src/*.c)
HDR      := $(wildcard src/*.h) src/methods.inc src/names.inc
TESTDIR  := ../../tests/ddraw_trace
TEST_SRC := $(wildcard $(TESTDIR)/*.c) $(wildcard $(TESTDIR)/*.h)

.PHONY: all test test-read-data test-unit test-game-run test-normalize check-tables gen-tables game-trace game-gate game-check game-gate-skirmish game-check-skirmish clean

all: $(BUILD)/ddraw.dll

$(BUILD):
	mkdir -p $@

$(BUILD)/ddraw.dll: $(SRC) $(HDR) ddraw.def | $(BUILD)
	$(CC) $(CFLAGS) -shared -s -o $@ $(SRC) ddraw.def -Wl,--enable-stdcall-fixup -static-libgcc -ldxguid -luser32 -lkernel32

gen-tables:
	python3 -I gen_tables.py "$(SDK_INC)" src

check-tables:
	python3 -I gen_tables.py "$(SDK_INC)" src --check

$(BUILD)/fakeplugin.dll: $(TESTDIR)/fakeplugin.c $(TESTDIR)/scenario.h | $(BUILD)
	$(CC) $(CFLAGS) -shared -s -o $@ $< -Wl,--enable-stdcall-fixup,--kill-at -static-libgcc -lddraw -ldxguid -luser32

# A proxy whose type map lacks LPDIRECTDRAWSURFACE7: the self-check must report it (test-unit runs the harness against it).
$(BUILD)/broken/ddraw.dll: $(SRC) $(HDR) ddraw.def | $(BUILD)
	mkdir -p $(BUILD)/broken
	$(CC) $(CFLAGS) -DDDT_TEST_BREAK_CLASSIFICATION -shared -s -o $@ $(SRC) ddraw.def -Wl,--enable-stdcall-fixup -static-libgcc -ldxguid -luser32 -lkernel32

$(BUILD)/mockddraw.dll: $(TESTDIR)/mockddraw.c | $(BUILD)
	$(CC) $(CFLAGS) -shared -s -o $@ $< -Wl,--enable-stdcall-fixup,--kill-at -static-libgcc -ldxguid -luser32

$(BUILD)/harness.exe: $(TESTDIR)/harness.c $(TESTDIR)/scenario.h | $(BUILD)
	$(CC) $(CFLAGS) -s -o $@ $< -Wl,--enable-stdcall-fixup -static-libgcc -lddraw -ldxguid -luser32

test: check-tables test-game-run test-normalize test-unit test-read-data

test-normalize:
	$(TESTDIR)/test-normalize.sh .

# The reference reader of the data file; also reads the file test-unit made (run after it).
test-read-data:
	$(TESTDIR)/test-read-data.sh .

# The shared game-run library and the drivers that use it, against a fake world (never the real game).
test-game-run:
	@for t in core snapshot wrapper trace-driver check-driver; do bash $(TESTDIR)/../game_run/test-$$t.sh || exit 1; done

# Test bed: our proxy as ddraw.dll, Wine's builtin ddraw copied under the name the proxy forwards to.
test-unit: $(BUILD)/ddraw.dll $(BUILD)/broken/ddraw.dll $(BUILD)/fakeplugin.dll $(BUILD)/harness.exe $(BUILD)/mockddraw.dll
	rm -rf $(TESTBED) && mkdir -p $(TESTBED)
	cp $(BUILD)/ddraw.dll $(BUILD)/fakeplugin.dll $(BUILD)/harness.exe $(BUILD)/mockddraw.dll $(TESTBED)/
	cp "$(WINE_ROOT)/lib/wine/i386-windows/ddraw.dll" $(TESTBED)/ddraw_next.dll
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=ddraw_next.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace.txt' \
	    "$(WINE)" harness.exe 2>/dev/null
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=mockddraw.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-mock.txt' \
	    "$(WINE)" harness.exe mock 2>/dev/null
	rm -f $(TESTBED)/trace-data-*.txt*
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=mockddraw.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-off.txt' \
	    "$(WINE)" harness.exe data-off 2>/dev/null
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=mockddraw.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-on.txt' \
	    DDT_DATA='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-on.txt' \
	    DDT_OFF_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-off.txt' \
	    "$(WINE)" harness.exe data 2>/dev/null
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=mockddraw.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 DDT_DATA_MAX=400 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-cap.txt' \
	    DDT_DATA='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-cap.txt' \
	    "$(WINE)" harness.exe data-cap 2>/dev/null
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=mockddraw.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-surf-off.txt' \
	    "$(WINE)" harness.exe data-surf-off 2>/dev/null
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=mockddraw.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=3 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-surf.txt' \
	    DDT_DATA='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-surf.txt' \
	    DDT_OFF_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-data-surf-off.txt' \
	    "$(WINE)" harness.exe data-surf 2>/dev/null
	rm -f $(TESTBED)/arm.flag $(TESTBED)/trace-arm.txt*
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=ddraw_next.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 DDT_MAX_PRESENTS=2 \
	    DDT_ARM_FILE='Z:$(subst /,\,$(abspath $(TESTBED)))\arm.flag' \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace-arm.txt' \
	    "$(WINE)" harness.exe arm 2>/dev/null
	rm -rf $(BUILD)/testbed-broken && mkdir -p $(BUILD)/testbed-broken
	cp $(BUILD)/broken/ddraw.dll $(BUILD)/harness.exe $(BUILD)/fakeplugin.dll $(BUILD)/testbed-broken/
	cd $(BUILD)/testbed-broken && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" "$(WINE)" harness.exe selfcheck-broken 2>/dev/null

game-trace: $(BUILD)/ddraw.dll
	scripts/game-trace.sh $(BUILD)/ddraw.dll

game-gate: $(BUILD)/ddraw.dll
	scripts/game-trace.sh $(BUILD)/ddraw.dll --gate

# One capture compared with the golden trace of this machine (display modes and Wine's GPU strings are machine specific).
game-check: $(BUILD)/ddraw.dll
	scripts/game-trace.sh $(BUILD)/ddraw.dll
	gzip -dc $(TESTDIR)/golden/startup-menu-n60.trace.gz | python3 -I scripts/normalize.py >$(BUILD)/golden.normalized.txt
	python3 -I scripts/normalize.py <$(BUILD)/game-trace/last.txt | diff $(BUILD)/golden.normalized.txt - >/dev/null \
	    && echo 'game-check: identical to the golden trace (cursor blits normalized)' || { echo 'game-check: DIFFERS from the golden trace'; exit 1; }

# Scenario 3: a saved skirmish loaded with -loadgame and paused (tests/ddraw_trace/scenarios.md).  The save is user data kept in
# $(HOME)/Games/EmpireEarth-re/scenarios (the driver checks its sha256); the golden is the structure form of the capture.
SKIRMISH := ../../tests/ddraw_trace/scenarios/skirmish-paused.env
SKIRMISH_OUT := $(BUILD)/game-trace-skirmish-paused

game-gate-skirmish: $(BUILD)/ddraw.dll
	scripts/game-trace.sh --scenario $(SKIRMISH) $(BUILD)/ddraw.dll --gate

game-check-skirmish: SHELL := /bin/bash
game-check-skirmish: $(BUILD)/ddraw.dll
	scripts/game-trace.sh --scenario $(SKIRMISH) $(BUILD)/ddraw.dll
	python3 -I scripts/normalize.py --structure <$(SKIRMISH_OUT)/last.txt | cmp - <(gzip -dc $(TESTDIR)/golden/skirmish-paused-n60.structure.trace.gz) \
	    && echo 'game-check-skirmish: identical to the golden (structure form)' || { echo 'game-check-skirmish: DIFFERS from the golden'; exit 1; }

clean:
	rm -rf $(BUILD)
