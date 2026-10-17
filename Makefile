# ---------------------------------------------------------------
# Makefile: raylib + libtorch game (fast incremental builds)
# ---------------------------------------------------------------

# System toolchain first (avoids the i686-elf ld shadowing problem)
export PATH := /usr/bin:/bin:$(PATH)

# Use all cores
MAKEFLAGS += -j$(shell nproc)

CXX      = g++
OPT     ?= -O2
CXXFLAGS = -Wall -Wextra $(OPT) -std=c++20 -MMD -MP
LDFUSE   = -B/usr/bin

# Pick the fastest linker available
ifneq ($(shell command -v mold 2>/dev/null),)
  LDFUSE += -fuse-ld=mold
else ifneq ($(shell command -v ld.lld 2>/dev/null),)
  LDFUSE += -fuse-ld=lld
endif

TARGET   = ./game

RAYLIB_ROOT  = $(HOME)/raylib
LIBTORCH_DIR = $(HOME)/libtorch/libtorch

# Auto-locate libraylib anywhere under ~/raylib (prefers .so, then .a)
RAYLIB_LIBFILE := $(firstword $(shell find $(RAYLIB_ROOT) \( -name 'libraylib.so' -o -name 'libraylib.so.*' -o -name 'libraylib.a' \) 2>/dev/null | sort))
ifeq ($(RAYLIB_LIBFILE),)
  $(error No libraylib found under $(RAYLIB_ROOT). Check where you extracted raylib)
endif
RAYLIB_LIBDIR := $(dir $(RAYLIB_LIBFILE))
RAYLIB_DIR    := $(patsubst %/,%,$(dir $(patsubst %/,%,$(RAYLIB_LIBDIR))))

INCLUDES = -isystem $(RAYLIB_DIR)/include \
           -isystem $(LIBTORCH_DIR)/include \
           -isystem $(LIBTORCH_DIR)/include/torch/csrc/api/include

LDFLAGS  = -L$(LIBTORCH_DIR)/lib \
           -Wl,-rpath,$(RAYLIB_LIBDIR) \
           -Wl,-rpath,$(LIBTORCH_DIR)/lib

TORCH_LIBS = -Wl,--no-as-needed -ltorch -ltorch_cpu -lc10 -Wl,--as-needed
SYS_LIBS   = -lpthread -ldl -lX11 -lGL -lm -lrt \
             -lXrandr -lXinerama -lXcursor -lXrender -lXext -lXi
# raylib linked by full path, so the exact filename doesn't matter
LIBS       = $(TORCH_LIBS) $(RAYLIB_LIBFILE) $(SYS_LIBS)

SRCS = $(sort $(filter-out pch.cpp,$(wildcard *.cpp)))
OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

PCH  = pch.h.gch

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(LDFUSE) $(LDFLAGS) $(LIBS)

# Precompiled header: torch + raylib parsed once
$(PCH): pch.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -x c++-header pch.h -o $(PCH)

# -include pch.h makes gcc silently use pch.h.gch for every file
%.o: %.cpp | $(PCH)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -include pch.h -c $< -o $@

clean:
	rm -f *.o *.d $(PCH) $(TARGET)

run: $(TARGET)
	$(TARGET)

which-ld:
	@which -a ld
	@$(CXX) $(LDFUSE) -print-prog-name=ld

-include $(DEPS)

.PHONY: all clean run which-ld