# Makefile for crypto-router backend

# Homebrew dependencies
BREW_DEPS := boost openssl simdjson libpq libpqxx

.DEFAULT_GOAL := all

check_deps:
	@# Ensure Homebrew exists
	@if ! command -v brew >/dev/null 2>&1; then \
		echo "Error: Homebrew not found. Please install Homebrew from https://brew.sh first."; \
		exit 1; \
	fi

	@echo "Checking required Homebrew packages..."
	@missing=0; \
	for pkg in $(BREW_DEPS); do \
		if ! brew list --versions $$pkg >/dev/null 2>&1; then \
			echo "  - Missing: $$pkg"; \
			missing=1; \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo ""; \
		echo "Some dependencies are missing."; \
		echo "Install them with:"; \
		echo "  brew install $(BREW_DEPS)"; \
		echo ""; \
		exit 1; \
	else \
		echo "All Homebrew dependencies are installed ✅"; \
	fi

# Detect library prefixes
BOOST_PREFIX := $(shell brew --prefix boost 2>/dev/null || echo "/usr/local")
OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null || echo "/usr/local")
SIMDJSON_PREFIX := $(shell brew --prefix simdjson 2>/dev/null || echo "/usr/local")
PQXX_PREFIX := $(shell brew --prefix libpqxx 2>/dev/null || echo "/usr/local")
PQ_PREFIX := $(shell brew --prefix libpq 2>/dev/null || echo "/usr/local")

# Compiler and flags
CXX := clang++
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra
INCLUDES := -I src \
            -I$(SIMDJSON_PREFIX)/include \
            -I$(BOOST_PREFIX)/include \
            -I$(OPENSSL_PREFIX)/include \
            -I$(PQXX_PREFIX)/include \
            -I$(PQ_PREFIX)/include

LIBS := -L$(BOOST_PREFIX)/lib -lboost_url \
        -L$(SIMDJSON_PREFIX)/lib -lsimdjson \
        -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto \
        -L$(PQXX_PREFIX)/lib -lpqxx \
        -L$(PQ_PREFIX)/lib -lpq

RPATH := -Wl,-rpath,$(SIMDJSON_PREFIX)/lib \
         -Wl,-rpath,$(OPENSSL_PREFIX)/lib \
         -Wl,-rpath,$(PQXX_PREFIX)/lib \
         -Wl,-rpath,$(PQ_PREFIX)/lib

DEFINES := -DBOOST_ERROR_CODE_HEADER_ONLY

# Source files
SOURCES := src/venues/coinbase/ws.cpp \
           src/venues/kraken/ws.cpp \
           src/md/symbol_codec.cpp \
           src/ui/master_feed.cpp \
           src/server/server_main.cpp

# Output
TARGET := build/server

# Default target
all: check_deps $(TARGET)

# Build the server
$(TARGET): $(SOURCES)
	@mkdir -p build
	@echo "Building $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(SOURCES) $(INCLUDES) $(LIBS) $(RPATH) $(DEFINES) -o $(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET)
	rm -rf build

# Rebuild (clean + build)
rebuild: clean all

# Run the server
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean rebuild run check_deps
