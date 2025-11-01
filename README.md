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

## Market data feed
- `ws` (websocket) performs pulling live data from exchanges
- `md` (market data) handles normalization of data streamed from above websocket datai