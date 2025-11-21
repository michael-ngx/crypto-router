# Makefile for crypto-router backend

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
SOURCES := src/ws/ws_coinbase.cpp \
           src/ws/ws_kraken.cpp \
           src/md/symbol_codec.cpp \
           src/pipeline/master_feed.cpp \
           src/server/server_main.cpp

# Output
TARGET := build/server

# Default target
all: $(TARGET)

# Build the server
$(TARGET): $(SOURCES)
	@mkdir -p build
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

.PHONY: all clean rebuild run

