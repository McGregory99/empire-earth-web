# Build and test the clean reimplementation of Empire Earth's `TCP Protocol.dll`.
#
#   make              build the plugin, the test harness and the negative-control stub
#   make test         run the differential test: harness(ours) must equal the golden transcript
#   make golden       regenerate tests/tcp_protocol/golden.txt from the ORIGINAL DLL (the oracle)
#   make determinism  run the harness twice against the original and require identical output
#   make test-stub    negative control: the broken stub must NOT match the golden transcript
#   make check-abi    compare exports, ordinals and imports of our DLL with the original
#   make wine-prefix  one-time setup of the private Wine prefix used for the tests
#
# Everything outside the repository (original game binaries, Wine prefix, test
# beds) is configured by the variables below and never written to the repo.  The
# differential test needs a network interface with a LAN address: the plugin's
# game discovery broadcasts UDP datagrams, and the harness contains a scenario for it.

# ---- toolchain ---------------------------------------------------------------
TARGET  := i686-w64-mingw32
CC      := $(TARGET)-gcc
DLLTOOL := $(TARGET)-dlltool
# Homebrew's mingw defaults to the UCRT; `msvcrt-os` links against the real
# msvcrt.dll so that memory crossing the DLL boundary shares the engine's heap.
CRT     := -mcrtdll=msvcrt-os
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -D_WIN32_WINNT=0x0501 $(CRT) -I src/engine_abi

# ---- locations outside the repository ---------------------------------------------
RE_DIR     ?= $(HOME)/Games/EmpireEarth-re
GAME_DIR   ?= $(HOME)/Games/EmpireEarth/Empire Earth
WINE       ?= $(HOME)/Library/Application Support/EmpireEarthMac/runtime/Wine Stable.app/Contents/Resources/wine/bin/wine
WINEPREFIX ?= $(RE_DIR)/wineprefix-test
TESTBED    ?= $(RE_DIR)/testbed
ORIGINAL_DLL := $(RE_DIR)/bin/TCP Protocol.dll
ENGINE_DLL   := $(RE_DIR)/bin/Low-Level Engine.dll
RUNTIME_DLL  := $(GAME_DIR)/sxlrt233.dll

export WINEPREFIX
export WINEDEBUG ?= -all

# ---- outputs --------------------------------------------------------------------
BUILD     := build
# GNU make needs the spaces of the DLL's file name escaped in targets and
# prerequisites; recipes use the unescaped *_PATH variants inside quotes.
PLUGIN      := $(BUILD)/ours/TCP\ Protocol.dll
PLUGIN_PATH := $(BUILD)/ours/TCP Protocol.dll
STUB        := $(BUILD)/stub/TCP\ Protocol.dll
STUB_PATH   := $(BUILD)/stub/TCP Protocol.dll
HARNESS   := $(BUILD)/harness.exe
ENGINE_LIB := $(BUILD)/libengine.a
GOLDEN    := tests/tcp_protocol/golden.txt

# RED stage: "ours" is still the failing stub, there is no reimplementation yet.
HARNESS_SOURCES := $(wildcard tests/tcp_protocol/*.c)
HARNESS_HEADERS := $(wildcard tests/tcp_protocol/*.h)
PLUGIN_SOURCES := $(wildcard src/plugins/tcp_protocol/*.c)
PLUGIN_HEADERS := src/engine_abi/low_level_engine.h

.PHONY: all test golden determinism test-stub check-abi wine-prefix clean testbed-ours testbed-original testbed-stub

all: $(PLUGIN) $(STUB) $(HARNESS)

$(BUILD)/ours $(BUILD)/stub:
	mkdir -p "$@"

# Import library for the engine's decorated C++ exports.
$(ENGINE_LIB): src/engine_abi/low_level_engine.def | $(BUILD)/ours
	$(DLLTOOL) --no-leading-underscore -d $< -l $@

$(PLUGIN): $(PLUGIN_SOURCES) $(PLUGIN_HEADERS) src/plugins/tcp_protocol/tcp_protocol.def $(ENGINE_LIB) | $(BUILD)/ours
	$(CC) $(CFLAGS) -shared -s -o "$@" $(PLUGIN_SOURCES) src/plugins/tcp_protocol/tcp_protocol.def \
	    -L $(BUILD) -lengine -lws2_32 -liphlpapi

$(STUB): tests/tcp_protocol/stub/stub.c src/plugins/tcp_protocol/tcp_protocol.def | $(BUILD)/stub
	$(CC) $(CFLAGS) -shared -o "$@" tests/tcp_protocol/stub/stub.c src/plugins/tcp_protocol/tcp_protocol.def

$(HARNESS): $(HARNESS_SOURCES) $(HARNESS_HEADERS) $(ENGINE_LIB) src/engine_abi/low_level_engine.h | $(BUILD)/ours
	$(CC) $(CFLAGS) -I tests/tcp_protocol -o $@ $(HARNESS_SOURCES) -L $(BUILD) -lengine -lws2_32

# ---- test beds: a directory per variant with the engine DLLs next to the plugin -------
# The plugin is loaded by full path, like the engine does; the engine DLLs it
# imports must be found next to it (altered search path).
define install_testbed
	mkdir -p "$(TESTBED)/$(1)"
	cp "$(ENGINE_DLL)" "$(TESTBED)/$(1)/Low-Level Engine.dll"
	cp "$(RUNTIME_DLL)" "$(TESTBED)/$(1)/sxlrt233.dll"
	cp "$(2)" "$(TESTBED)/$(1)/TCP Protocol.dll"
	cp $(HARNESS) "$(TESTBED)/$(1)/harness.exe"
endef

testbed-original: $(HARNESS)
	$(call install_testbed,original,$(ORIGINAL_DLL))

testbed-ours: $(PLUGIN) $(HARNESS)
	$(call install_testbed,ours,$(PLUGIN_PATH))

testbed-stub: $(STUB) $(HARNESS)
	$(call install_testbed,stub,$(STUB_PATH))

# run_harness <variant> <output file>: stdout is the transcript (Wine's chatter goes to stderr).
# The perl wrapper stops a hung run after 5 minutes.
HARNESS_TIMEOUT ?= 300
define run_harness
	cd "$(TESTBED)/$(1)" && perl -e 'alarm shift; exec @ARGV' $(HARNESS_TIMEOUT) "$(WINE)" harness.exe "TCP Protocol.dll" \
	    > "$(CURDIR)/$(2)" 2>/dev/null; \
	    status=$$?; test $$status -eq 0 || { echo "harness exited with status $$status (see $(2))"; exit 1; }
endef

test: testbed-ours
	$(call run_harness,ours,$(BUILD)/transcript-ours.txt)
	diff -u $(GOLDEN) $(BUILD)/transcript-ours.txt && echo "PASS: transcript of our DLL equals the golden transcript"

golden: testbed-original
	$(call run_harness,original,$(GOLDEN))
	@echo "wrote $(GOLDEN) from the original DLL"

determinism: testbed-original
	$(call run_harness,original,$(BUILD)/transcript-original-1.txt)
	$(call run_harness,original,$(BUILD)/transcript-original-2.txt)
	diff -u $(BUILD)/transcript-original-1.txt $(BUILD)/transcript-original-2.txt && echo "PASS: two runs against the original are identical"

test-stub: testbed-stub
	$(call run_harness,stub,$(BUILD)/transcript-stub.txt)
	@if diff -q $(GOLDEN) $(BUILD)/transcript-stub.txt >/dev/null; then \
	    echo "FAIL: the broken stub matches the golden transcript, the harness is too weak"; exit 1; \
	  else echo "PASS: the broken stub is detected (transcripts differ)"; fi

check-abi: $(PLUGIN)
	python3 -I tools/re/pe_export_names.py "$(ORIGINAL_DLL)" > $(BUILD)/export-names-original.txt
	python3 -I tools/re/pe_export_names.py "$(PLUGIN_PATH)" > $(BUILD)/export-names-ours.txt
	diff -u $(BUILD)/export-names-original.txt $(BUILD)/export-names-ours.txt && echo "PASS: identical export names"
	python3 -I tools/re/pe_export_ordinals.py "$(ORIGINAL_DLL)" > $(BUILD)/exports-original.txt
	python3 -I tools/re/pe_export_ordinals.py "$(PLUGIN_PATH)" > $(BUILD)/exports-ours.txt
	diff -u $(BUILD)/exports-original.txt $(BUILD)/exports-ours.txt && echo "PASS: identical export ordinals"
	python3 -I tools/re/pe_import_diff.py "$(ORIGINAL_DLL)" "$(PLUGIN_PATH)"
	@if python3 -I tools/re/pe_imports.py "$(PLUGIN_PATH)" | grep -qiE 'api-ms-win-crt|ucrtbase'; then \
	    echo "FAIL: the plugin imports the UCRT"; exit 1; \
	  else echo "PASS: the plugin imports msvcrt.dll, not the UCRT"; fi

# Create the Wine prefix the tests run in and make crashes terminate instead of
# waiting for an interactive debugger.  Safe to repeat.
wine-prefix:
	mkdir -p "$(WINEPREFIX)"
	"$(WINE)" wineboot -i > /dev/null 2>&1
	for key in 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug' \
	           'HKLM\Software\Wow6432Node\Microsoft\Windows NT\CurrentVersion\AeDebug'; do \
	    "$(WINE)" reg add "$$key" /v Debugger /t REG_SZ /d 'cmd.exe /c exit' /f > /dev/null 2>&1; \
	    "$(WINE)" reg add "$$key" /v Auto /t REG_SZ /d 1 /f > /dev/null 2>&1; \
	done

clean:
	rm -rf $(BUILD)
