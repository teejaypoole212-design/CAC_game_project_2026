# ---------------------------------------------------------------
# Makefile: raylib + libtorch game
# Fixes "ld: unrecognised emulation mode: elf_x86_64" caused by the
# i686-elf cross-linker shadowing the system linker in PATH.
# ---------------------------------------------------------------

# Put the system toolchain first so gcc finds /usr/bin/ld, not
# /usr/local/i686-elf/bin/ld. This only affects processes run by make.
export PATH := /usr/bin:/bin:$(PATH)

# 1. Compiler and general settings
CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++20 -MMD -MP

# Belt and braces: tell gcc to look for ld, as, etc. in /usr/bin first
LDFUSE   = -B/usr/bin

# 2. Target executable name
TARGET   = ./game

# 3. Path configuration
RAYLIB_DIR   = $(HOME)/raylib/raylib-6.0_linux_amd64
LIBTORCH_DIR = $(HOME)/libtorch/libtorch

# 4. Include and library paths
INCLUDES = -I$(RAYLIB_DIR)/include \
           -I$(LIBTORCH_DIR)/include \
           -I$(LIBTORCH_DIR)/include/torch/csrc/api/include

LDFLAGS  = -L$(RAYLIB_DIR)/lib \
           -L$(LIBTORCH_DIR)/lib \
           -Wl,-rpath,$(RAYLIB_DIR)/lib \
           -Wl,-rpath,$(LIBTORCH_DIR)/lib

# 5. Libraries to link
# --no-as-needed stops the linker from dropping libtorch libs that
# are only needed for static initialisation (op registration).
TORCH_LIBS = -Wl,--no-as-needed -ltorch -ltorch_cpu -lc10 -Wl,--as-needed
SYS_LIBS   = -lraylib -lpthread -ldl -lX11 -lGL -lm -lrt \
             -lXrandr -lXinerama -lXcursor -lXrender -lXext -lXi
LIBS       = $(TORCH_LIBS) $(SYS_LIBS)

# 6. Automatic file tracking
SRCS = $(sort $(wildcard *.cpp))
OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

# 7. Build rules
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(LDFUSE) $(LDFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f *.o *.d $(TARGET)

run: $(TARGET)
	$(TARGET)

# Debug helper: shows which linker will actually be used
which-ld:
	@which -a ld
	@$(CXX) $(LDFUSE) -print-prog-name=ld

-include $(DEPS)

.PHONY: all clean run which-ld