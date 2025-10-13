# Run project

Run `bash`

Backend 
```bash
cd backend
cmake -S . -B build
cmake --build build -j
./build/bin/router
```

Frontend
```bash
cd frontend
pnpm install   # or npm i / yarn
pnpm dev
```

# Dev notes

- "Memory" means keeping all data purely in RAM, so the "store" is volatile and exists inly while the program runs
- There is one `MemoryStore` instance per process (application instance)

### Test 1: Storage code

```bash
cd backend
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/storage_memory.cpp src/test_storage.cpp -I src \
  -o build/test_storage

./build/test_storage
```

### Test 2: Socket pulling live data from Coinbase

```bash
cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/web_socket.cpp src/test_socket.cpp \
  -I src -I"$BOOST_PREFIX/include" -I"$OPENSSL_PREFIX/include" \
  "$OPENSSL_PREFIX/lib/libssl.dylib" \
  "$OPENSSL_PREFIX/lib/libcrypto.dylib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/test_socket

./build/test_socket
```