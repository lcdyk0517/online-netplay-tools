# Netplay lobby browser.
#
# Default: RK3326 SDK toolchain (gcc 8.3 + glibc 2.28 sysroot) so the binary
# also runs on older ArkOS bases (max symbol GLIBC_2.27).
#   make            -> netplay
#   make SHOT=1     -> netplay-shot (software renderer, RA_LOBBY_SHOT env)
#   make MODERN=1   -> build with the 3562 gcc-15 wrapper/sysroot instead

SDK     ?= /home/lcdyk/libretro/rk3326-sdk-proper/gcc-aarch64
SYSROOT := $(SDK)/aarch64-linux-gnu/libc

ifdef MODERN
CXX      := /home/lcdyk/sdl2_xbuild/bin/ac-g++
CXXFLAGS := -O2 -std=c++17 -pthread -MMD -MP -Isrc -I/home/lcdyk/dolphin-sysroot/usr/include
LDFLAGS  := -pthread
LDLIBS   := -Ldeps -lSDL2_ttf-2.0 -lSDL2 -lcurl -lz
else
CXX      := $(SDK)/bin/aarch64-linux-gnu-g++
CXXFLAGS := -O2 -std=c++17 -pthread -MMD -MP -Isrc \
            --sysroot=$(SYSROOT) -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/SDL2
LDFLAGS  := -pthread --sysroot=$(SYSROOT)
LDLIBS   := -L$(SYSROOT)/usr/lib/aarch64-linux-gnu -L$(SYSROOT)/lib/aarch64-linux-gnu \
            -lSDL2_ttf-2.0 -lSDL2 -lcurl -lz
endif

BIN := netplay
ifdef SHOT
CXXFLAGS += -DRA_SHOT
BIN := netplay-shot
endif

SRCS := $(wildcard src/*.cpp)
BUILD := build$(if $(SHOT),-shot,)
OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: src/%.cpp src/*.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(DEPS)

clean:
	rm -rf build build-shot netplay netplay-shot

.PHONY: all clean
