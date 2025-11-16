# Cryptocurrency Order Routing System


##  1. <a name='TODO'></a>TODO
- Frontend
    - [ ] Fix backend market data API to allow switching currency pairs and allowed venues
- Market data
    - [ ] Extend available venue list
- Trade
    - [ ] Entire decision making algorithm
    - [ ] Send test orders and receive confirmation from sandbox environments
    - [ ] Analytics dash board

##  2. <a name='Modules'></a>Modules

- "Memory" means keeping all data purely in RAM, so the "store" is volatile and exists inly while the program runs
- There is one `MemoryStore` instance per process (application instance)

###  2.1. <a name='Marketdatafeed'></a>Market data feed
- `ws` (websocket) performs pulling live data from exchanges
- `md` (market data) handles normalization of data streamed from above websocket data

###  2.2. <a name='Orders'></a>Orders

- Market order: Check if market protect order is available by exchanges (preferred)
Accept that slippage happens...
- Limit order: Manage limit orders separately in our order system. When limit order matches market, turn into market order. 


###  2.3. <a name='Runapp'></a>Run app

- Backend  
```bash
cd backend
BOOST_PREFIX=$(brew --prefix boost)
OPENSSL_PREFIX=$(brew --prefix openssl)
SIMDJSON_PREFIX=$(brew --prefix simdjson)
mkdir -p build

clang++ -std=c++20 -O3 -Wall -Wextra \
  src/ws/ws_coinbase.cpp src/ws/ws_kraken.cpp \
  src/md/symbol_codec.cpp \
  src/pipeline/master_feed.cpp \
  src/server/server_main.cpp \
  -I src \
  -I"$SIMDJSON_PREFIX/include" \
  -I"$BOOST_PREFIX/include" \
  -L"$BOOST_PREFIX/lib" -lboost_url \
  -I"$OPENSSL_PREFIX/include" \
  -L"$SIMDJSON_PREFIX/lib" -lsimdjson \
  -L"$OPENSSL_PREFIX/lib" -lssl -lcrypto \
  -Wl,-rpath,"$SIMDJSON_PREFIX/lib" \
  -Wl,-rpath,"$OPENSSL_PREFIX/lib" \
  -DBOOST_ERROR_CODE_HEADER_ONLY \
  -o build/server

./build/server
```

- Frontend
```bash
cd frontend
npm install
npm run dev
```
