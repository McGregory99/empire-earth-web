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
# Renderer plugin spike (clang, MSVC C++ ABI; see src/plugins/dx7_rasterizer/):
#   make rasterizer             build EEWebRasterizer.dll
#   make check-abi-rasterizer   exports, ordinals and imports of that DLL
#   make test-rasterizer        harness against the original DX7HRTnLDisplay.dll and ours, under Wine
#   make game-check-rasterizer  automated in-game check (takes the game-run lock, restores the game dir)
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
# Always the project's own prefix (":=", not "?="): an ambient WINEPREFIX, typically the game's, must
# never receive the registry changes of `make wine-prefix` or run the tests.  Override on the make
# command line (`make WINEPREFIX=...`) if you really mean to.
WINEPREFIX := $(RE_DIR)/wineprefix-test
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

# Every source depends on every header of its program: the DLL and the harness are built in one
# compiler invocation, so a stale build after a header edit would let `make test` pass on old code.
HARNESS_SOURCES := $(wildcard tests/tcp_protocol/*.c)
HARNESS_HEADERS := $(wildcard tests/tcp_protocol/*.h) src/engine_abi/low_level_engine.h
PLUGIN_SOURCES := $(wildcard src/plugins/tcp_protocol/*.c)
PLUGIN_HEADERS := $(wildcard src/plugins/tcp_protocol/*.h) src/engine_abi/low_level_engine.h

.PHONY: all test golden determinism test-stub check-abi wine-prefix clean testbed-ours testbed-original testbed-stub \
    rasterizer check-abi-rasterizer test-rasterizer game-check-rasterizer testbed-rasterizer

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

$(HARNESS): $(HARNESS_SOURCES) $(HARNESS_HEADERS) $(ENGINE_LIB) | $(BUILD)/ours
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

# ---- renderer plugin spike (P2.1): clang with the MSVC C++ ABI ------------------------------
# The real engine loads rasterizer plugins that export MSVC-mangled C++ and share vtables, RTTI
# and the heap with it, so the plugin is compiled with clang for i686-pc-windows-msvc and linked
# by lld-link against the VC6-era msvcrt.dll only: the import libraries below come from the .def
# files, never from a Windows SDK or the UCRT.  The test harness is plain C built with mingw.
# Tools: Homebrew `llvm` (clang++, llvm-dlltool) and `lld` (lld-link): `brew install llvm lld`.
# /brepro makes the link reproducible (no timestamp), so equal sources give an equal sha256.
CLANGXX      ?= /opt/homebrew/opt/llvm/bin/clang++
LLD_LINK     ?= /opt/homebrew/opt/lld/bin/lld-link
LLVM_DLLTOOL ?= /opt/homebrew/opt/llvm/bin/llvm-dlltool

RAST_SRC       := src/plugins/dx7_rasterizer
RAST_BUILD     := $(BUILD)/rasterizer
RAST_DLL_NAME  := EEWebRasterizer.dll
RAST_DLL       := $(RAST_BUILD)/$(RAST_DLL_NAME)
RAST_SOURCES   := $(wildcard $(RAST_SRC)/*.cpp)
RAST_HEADERS   := $(wildcard $(RAST_SRC)/*.h)
RAST_OBJECTS   := $(patsubst $(RAST_SRC)/%.cpp,$(RAST_BUILD)/%.obj,$(RAST_SOURCES))
RAST_ORIGINAL  := $(RE_DIR)/bin/DX7HRTnLDisplay.dll
RAST_HARNESS   := $(BUILD)/rasterizer_harness.exe
RAST_TESTBED   := $(TESTBED)/rasterizer
RAST_GAME_CHECK := tests/dx7_rasterizer/game_check.sh

# -fno-sized-deallocation: VC6's msvcrt.dll has no `operator delete(void*, size_t)`.
# -fno-threadsafe-statics, -fno-exceptions: no CRT support for guards / unwinding in this DLL.
# -fcheck-new: VC6's operator new returns NULL instead of throwing.
# -march=i686 -mno-sse -ffp-contract=off: x87 code like MSVC 6 (the engine sets the x87 precision).
RAST_CXXFLAGS := --target=i686-pc-windows-msvc -std=c++14 -O2 -Wall -Wextra -ffreestanding \
    -fno-exceptions -frtti -fno-sized-deallocation -fno-threadsafe-statics -fcheck-new \
    -fno-stack-protector -march=i686 -mno-sse -ffp-contract=off

rasterizer: $(RAST_DLL)

$(RAST_BUILD):
	mkdir -p "$@"

# Import libraries from .def files; `-D` names the DLL the linker records in the import table.
$(RAST_BUILD)/msvcrt.lib: $(RAST_SRC)/msvcrt_vc6.def | $(RAST_BUILD)
	$(LLVM_DLLTOOL) -m i386 -d $< -l $@ -D msvcrt.dll

$(RAST_BUILD)/lowlevel.lib: $(RAST_SRC)/lle_imports.def | $(RAST_BUILD)
	$(LLVM_DLLTOOL) -m i386 -d $< -l $@ -D "Low-Level Engine.dll"

$(RAST_BUILD)/%.obj: $(RAST_SRC)/%.cpp $(RAST_HEADERS) | $(RAST_BUILD)
	$(CLANGXX) $(RAST_CXXFLAGS) -c -o $@ $<

$(RAST_DLL): $(RAST_OBJECTS) $(RAST_SRC)/eewebrasterizer.def $(RAST_BUILD)/msvcrt.lib $(RAST_BUILD)/lowlevel.lib
	$(LLD_LINK) /dll /nodefaultlib /entry:DllMain /def:$(RAST_SRC)/eewebrasterizer.def /out:$@ \
	    /subsystem:windows,5.0 /opt:ref /filealign:512 /brepro $(RAST_OBJECTS) \
	    $(RAST_BUILD)/msvcrt.lib $(RAST_BUILD)/lowlevel.lib

$(RAST_HARNESS): tests/dx7_rasterizer/harness.c src/engine_abi/low_level_engine.h $(RAST_SRC)/rasterizer_abi.h $(ENGINE_LIB)
	$(CC) $(CFLAGS) -I $(RAST_SRC) -o $@ $< -L $(BUILD) -lengine

# Exports, ordinals and imports of our DLL against the contract (tests/dx7_rasterizer/).
check-abi-rasterizer: $(RAST_DLL)
	python3 -I tools/re/pe_export_ordinals.py "$(RAST_DLL)" > $(RAST_BUILD)/exports-ours.txt
	diff -u tests/dx7_rasterizer/expected_exports.txt $(RAST_BUILD)/exports-ours.txt && echo "PASS: exports and ordinals are as defined"
	python3 -I tests/dx7_rasterizer/check_imports.py "$(RAST_DLL)" --require $(RAST_REQUIRED_IMPORTS)

# msvcrt.dll imports that prove the C++ constructs were really compiled and linked: pure virtual
# slots, dynamic_cast, operator new and delete.  Quoted because of the `?` and `@` characters.
RAST_REQUIRED_IMPORTS := '_purecall' '__RTDynamicCast' '??2@YAPAXI@Z' '??3@YAXPAX@Z'

# Test beds: the engine DLLs sit next to the plugin (altered search path), like in the game dir.
testbed-rasterizer: $(RAST_DLL) $(RAST_HARNESS)
	mkdir -p "$(RAST_TESTBED)/original" "$(RAST_TESTBED)/ours"
	cp "$(ENGINE_DLL)" "$(RAST_TESTBED)/original/Low-Level Engine.dll"
	cp "$(RUNTIME_DLL)" "$(RAST_TESTBED)/original/sxlrt233.dll"
	cp "$(GAME_DIR)/binkw32.dll" "$(RAST_TESTBED)/original/binkw32.dll"
	cp "$(RAST_ORIGINAL)" "$(RAST_TESTBED)/original/DX7HRTnLDisplay.dll"
	cp $(RAST_HARNESS) "$(RAST_TESTBED)/original/rasterizer_harness.exe"
	cp "$(ENGINE_DLL)" "$(RAST_TESTBED)/ours/Low-Level Engine.dll"
	cp "$(RUNTIME_DLL)" "$(RAST_TESTBED)/ours/sxlrt233.dll"
	cp "$(RAST_DLL)" "$(RAST_TESTBED)/ours/$(RAST_DLL_NAME)"
	cp $(RAST_HARNESS) "$(RAST_TESTBED)/ours/rasterizer_harness.exe"

# run_rast_harness <variant> <dll file name>: the transcript goes to the terminal and a file.
define run_rast_harness
	cd "$(RAST_TESTBED)/$(1)" && perl -e 'alarm shift; exec @ARGV' $(HARNESS_TIMEOUT) "$(WINE)" rasterizer_harness.exe $(1) "$(2)" \
	    2>/dev/null | tee "$(CURDIR)/$(BUILD)/rasterizer-$(1).txt"; \
	    status=$${PIPESTATUS[0]}; test $$status -eq 0 || { echo "FAIL: $(1) harness exited with status $$status"; exit 1; }
endef

test-rasterizer: SHELL := /bin/bash
test-rasterizer: check-abi-rasterizer testbed-rasterizer
	$(call run_rast_harness,original,DX7HRTnLDisplay.dll)
	$(call run_rast_harness,ours,$(RAST_DLL_NAME))
	@echo "PASS: the original rasterizer plugin and ours both satisfy the plugin contract"

game-check-rasterizer: $(RAST_DLL)
	bash $(RAST_GAME_CHECK) "$(RAST_DLL)" "$(RAST_DLL_NAME)"

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
