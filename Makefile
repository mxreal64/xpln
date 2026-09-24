# ==============================================================================
# PLIC - C++23 PL/I Compiler Makefile
# ==============================================================================

# Compiler & Linker Settings
CXX          := g++
CXXFLAGS     := -std=c++23 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS      :=

# Configuration Options: make BUILD=debug or make BUILD=release (default)
BUILD        ?= release

ifeq ($(BUILD),debug)
    CXXFLAGS += -g -O0 -DDEBUG -fsanitize=address,undefined
    LDFLAGS  += -fsanitize=address,undefined
    BUILD_DIR := build/debug
else
    CXXFLAGS += -O3 -flto -DNDEBUG
    LDFLAGS  += -O3 -flto
    BUILD_DIR := build/release
endif

# Target Executable Name
TARGET       := $(BUILD_DIR)/xpln

# Source & Object Directories
SRC_DIR      := src
SRCS         := $(wildcard $(SRC_DIR)/*.cpp)
OBJS         := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS         := $(OBJS:.o=.d)

# Include flags
INCLUDES     := -I$(SRC_DIR)

# Default Rule
.PHONY: all
all: $(TARGET)

# Link Binary
$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "==> Linking target: $@"
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Compile C++ Source Files to Objects with Auto-Generated Dependency Tracking (.d)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "==> Compiling: $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Include generated header dependency rules
-include $(DEPS)

# Helper Rule: Compile PL/I file to x86-64 Assembly (.s)
# Usage: make asm FILE=tests/example.pli
.PHONY: asm
asm: $(TARGET)
ifndef FILE
	$(error Please specify a PL/I file using FILE=<path_to_pli_file>)
endif
	@echo "==> Compiling PL/I file '$(FILE)' to assembly..."
	./$(TARGET) $(FILE) > $(basename $(FILE)).s
	@echo "==> Generated: $(basename $(FILE)).s"

# Helper Rule: Build and assemble/link a PL/I file directly into an ELF executable
# Usage: make exec FILE=tests/example.pli
.PHONY: exec
exec: asm
	@echo "==> Assembling and linking ELF binary..."
	$(CC) -no-pie $(basename $(FILE)).s -o $(basename $(FILE))
	@echo "==> Executable created: $(basename $(FILE))"

# Clean Build Artifacts
.PHONY: clean
clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf build

# Rebuild Entire Project
.PHONY: re
re: clean all

# Print help/usage instructions
.PHONY: help
help:
	@echo "PLIC Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all             Build the plic compiler binary (default)"
	@echo "  clean           Remove object files and output binaries"
	@echo "  re              Rebuild the project completely"
	@echo "  asm FILE=path   Compile a .pli file into x86-64 assembly (.s)"
	@echo "  exec FILE=path  Compile a .pli file, assemble it, and link it into an ELF binary"
	@echo ""
	@echo "Options:"
	@echo "  BUILD=release   Build with -O3 -flto optimization (default)"
	@echo "  BUILD=debug     Build with -O0 -g and AddressSanitizer"
