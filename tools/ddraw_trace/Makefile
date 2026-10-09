# Tracing DirectDraw proxy: an oracle for the Direct3D 7 renderer plugin.
#
#   make              build build/ddraw.dll (the proxy)
#   make test         unit tests: harness + fake plugin under Wine in front of Wine's builtin ddraw
#   make check-tables regenerate src/*.inc from the SDK headers and compare
#   make gen-tables   regenerate src/methods.inc and src/names.inc
#   make game-trace   one in-game capture (takes the game-run lock, launches the game, ~3 min); compares with the previous run
#   make game-gate    two in-game captures and their diff: the determinism gate
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

.PHONY: all test test-unit test-restore check-tables gen-tables game-trace game-gate clean

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

$(BUILD)/harness.exe: $(TESTDIR)/harness.c $(TESTDIR)/scenario.h | $(BUILD)
	$(CC) $(CFLAGS) -s -o $@ $< -Wl,--enable-stdcall-fixup -static-libgcc -lddraw -ldxguid -luser32

test: test-restore test-unit

test-restore:
	$(TESTDIR)/test-restore.sh .

# Test bed: our proxy as ddraw.dll, Wine's builtin ddraw copied under the name the proxy forwards to.
test-unit: $(BUILD)/ddraw.dll $(BUILD)/fakeplugin.dll $(BUILD)/harness.exe
	rm -rf $(TESTBED) && mkdir -p $(TESTBED)
	cp $(BUILD)/ddraw.dll $(BUILD)/fakeplugin.dll $(BUILD)/harness.exe $(TESTBED)/
	cp "$(WINE_ROOT)/lib/wine/i386-windows/ddraw.dll" $(TESTBED)/ddraw_next.dll
	cd $(TESTBED) && WINEDLLOVERRIDES="ddraw=n;mscoree,mshtml=" \
	    DDT_NEXT=ddraw_next.dll DDT_MODULES=fakeplugin.dll DDT_FLUSH=1 \
	    DDT_TRACE='Z:$(subst /,\,$(abspath $(TESTBED)))\trace.txt' \
	    "$(WINE)" harness.exe 2>/dev/null

game-trace: $(BUILD)/ddraw.dll
	scripts/game-trace.sh $(BUILD)/ddraw.dll

game-gate: $(BUILD)/ddraw.dll
	scripts/game-trace.sh $(BUILD)/ddraw.dll --gate

clean:
	rm -rf $(BUILD)
