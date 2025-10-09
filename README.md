# Run project

Run `bash`

Test storage code
```bash
cd backend
clang++ -std=c++20 -O3 -Wall -Wextra \
  src/storage_memory.cpp src/test_storage.cpp -I src \
  -o build/test_storage
./build/test_storage
```

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
