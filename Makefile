# 1. Compiler and General Settings
CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++20

# 2. Target Executable Name
TARGET   = ./game

# 3. Path Configurations (Based on your system layout)
RAYLIB_DIR   = $(HOME)/raylib/raylib-6.0_linux_amd64
LIBTORCH_DIR = $(HOME)/Downloads/libtorch/

# 4. Include and Library Paths (Tells compiler where headers/libs live)
INCLUDES = -I$(RAYLIB_DIR)/include \
           -I$(LIBTORCH_DIR)/include \
           -I$(LIBTORCH_DIR)/include/torch/csrc/api/include

LDFLAGS  = -L$(RAYLIB_DIR)/lib \
           -L$(LIBTORCH_DIR)/lib \
           -Wl,-rpath,$(RAYLIB_DIR)/lib \
           -Wl,-rpath,$(LIBTORCH_DIR)/lib

# 5. Libraries to Link
LIBS     = -lraylib -ltorch -ltorch_cpu -lc10 -lpthread -ldl -lX11 -lGL -lm -lpthread -lrt -lXrandr -lXinerama -lXcursor -lXrender -lXext -lXi

# 6. Automatic File Tracking
# Simply add any new .cpp files to this directory; the script finds them instantly.
SRCS     = main.cpp ai.cpp $(wildcard *.cpp)
# Filters out duplicates if main.cpp is caught twice by wildcard
SRCS     := $(sort $(SRCS))
OBJS     = $(SRCS:.cpp=.o)

# 7. Build Rules
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean

run: $(TARGET)
	$(TARGET)

.PHONY: all clean run