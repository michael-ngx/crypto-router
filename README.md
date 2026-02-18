# Cryptocurrency Order Routing System


##  1. <a name='TODO'></a>TODO
- Market data
    - [ ] Extend available venue list
    - [ ] Implement checks for venue's crypto pair support
- Trade
    - [ ] Entire decision making algorithm
    - [ ] Send test orders and receive confirmation from sandbox environments
    - [ ] Analytics dash board

##  2. <a name='Modules'></a>Modules

- "Memory" means keeping all data purely in RAM, so the "store" is volatile and exists inly while the program runs
- There is one `MemoryStore` instance per process (application instance)

### <a name='Marketdatafeed'></a>Market data feed
- `ws` (websocket) performs pulling live data from exchanges
- `md` (market data) handles normalization of data streamed from above websocket data

### <a name='Orders'></a>Orders

- Market order: Check if market protect order is available by exchanges (preferred)
Accept that slippage happens...
- Limit order: Manage limit orders separately in our order system. When limit order matches market, turn into market order. 


### <a name='Runapp'></a>Run app

- Backend  
```bash
cd backend
make
./build/server
```

- Frontend
```bash
cd frontend
npm install
npm run dev
```
