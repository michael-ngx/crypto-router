
### <a name='Orders'></a>Orders
- **Market order**: Check if market protect order is available by exchanges (preferred). We tries our best in the routing algorithm, but have to accept that slippage happens...
- **Limit order**: Manage limit orders separately in our order system. When limit order matches market, turn into market order.

### <a name='Marketdatafeed'></a>Market data feed
- `ws` (websocket) performs pulling live data from exchanges
- `md` (market data) handles normalization of data streamed from above websocket data


### 1. **UIMasterFeed** and **IVenueFeed**

Each `IVenueFeed` ties to <mark>**ONE canonical pair ON ONE VENUE**</mark>
Each `UIMasterFeed` ties to <mark>**ONE** **canonical pair**.</mark>, and is registered by multiple `IVenueFeed`s

> [!NOTE] UIMasterFeed
> 
> `UIMasterFeed` is simply an UI component that displays the consolidated top of these `IVenueFeed`s

> [!NOTE] IVenueFeed
> `IVenueFeed` contains SPSC queues connecting to trading venues, and contains its own `Book`, as well as 1 SPSC queue.
> 
> Book updates are pushed into the queue. `VenueFeed`'s `"consume_loop()"` function:
> - Frequently takes update out from the queue
> - Acquire Book's lock and apply the change to book
> - <mark>Publish the venue's "top snap shot" atomically <--- This is what HTTP Read APIs read</mark>. *This design ensures that 10,000 users DO NOT directly touch the venue's book, and possibly interfere with book write paths*


### 2. **FeedManager**

Not all `IVenueFeed`s are ran at all times. We only keep "hot" trading pairs up and running, thus subscribe to new pairs if user selects them. <mark>The `FeedManager`, is responsible for handling these starting/getting requests. That's why it's tied to HTTP's `handle_request()`</mark>

- On‑demand feed manager with idle sweeping/pinning: `feed_manager.hpp`
- `server_main` builds venue at runtimes, configures feed manager via `.env`, and uses it in HTTP handler
- `/api/book` triggers `get_or_subscribe`, `/api/pairs` returns all supported pairs (not just active)

<span style="color: red;">**IMPORTANT:**</span> <mark>Additional guard needed to be implemented, to avoid cancelling crypto pairs that are "in-flight"</mark> (being routed/executed)

#### Entry
This is what lives **inside** **FeedManager**s. An `Entry` means “one active pair subscription” inside `entries_`, keyed by symbol.

Field meaning ([feed_manager.hpp (line 187)](https://file+.vscode-resource.vscode-cdn.net/Users/mnguyen/.vscode/extensions/openai.chatgpt-0.4.74-darwin-arm64/webview/# "backend/src/server/feed_manager.hpp (line 187)")):

- **symbol**: pair id (e.g. BTC-USD), stored so logs still know the pair even after moving out of the map.
- **ui**: the pair’s UIMasterFeed object returned to /api/book.
- **feeds**: all live per-venue feeds for that pair (Coinbase/Kraken/etc.), used to stop them later.
- **last_access**: last time this pair was requested via get_or_subscribe.
- **pinned**: hot/prewarmed flag; pinned pairs are exempt from idle sweep.

**Lifecycle**:
- Created on first prewarm or first /api/book for that pair
- Reused on subsequent requests; only last_access is refreshed
- For non-pinned pairs, sweep removes/stops entry after idle timeout
- On server shutdown, all remaining entries are stopped



### 3. Registering venues

We've developed a structured method to register new venues. 

a) Add venue to `/server/venues_config.hpp`

b) Create the following files:
- `api.hpp`: create a class implementing `IVenueApi` interface, to handle API calls to venue site, like checking if the platform supports a pair
- `factory.hpp`: Implement a function returning `VenueFactory`  class. For example:
```cpp
inline VenueFactory make_coinbase_factory();
```

- `parser.hpp`: create a class implementing `IBookParser` interface, responsible for parsing raw message from venue into `BookEvent`(s)
- `ws.hpp` and `ws.cpp`: create a class implementing `IMarketWs` interface, to handle websocket connections to the venues.

c) **THEN**, register the make venue factor function in `venue_registry.hpp`. This will make sure that the `VenueFactory` function you created above ***will be called*** whenever a crypto pair is created/selected.
	As a reminder, each crypto pair will attempt to create a `VenueFeed`, through `VenueFactory` function, for **each** venues