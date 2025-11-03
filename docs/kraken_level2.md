## Kraken Market Data API

[Link](https://docs.kraken.com/api/docs/websocket-v2/book)

The `book` channel streams level 2 (L2) order book. It describes the individual price levels in the book with aggregated order quantities at each level.

#### Snapshot response
```json
{
    "channel": "book",
    "type": "snapshot",
    "data": [
        {
            "symbol": "MATIC/USD",
            "bids": [
                {
                    "price": 0.5666,
                    "qty": 4831.75496356
                },
                {
                    "price": 0.5665,
                    "qty": 6658.22734739
                }
            ],
            "asks": [
                {
                    "price": 0.5668,
                    "qty": 4410.79769741
                },
                {
                    "price": 0.5669,
                    "qty": 4655.40412487
                }
            ],
            "checksum": 2439117997
        }
    ]
}
```


#### Update response

The data contains the updates of the bids and asks for the relevant symbol including a CRC32 checksum of the top 10 bids and asks.

Note, it is possible to have multiple updates to the same price level in a single update message. Updates should always be processed in sequence.

```json
{
    "channel": "book",
    "type": "update",
    "data": [
        {
            "symbol": "MATIC/USD",
            "bids": [
                {
                    "price": 0.5657,
                    "qty": 1098.3947558
                }
            ],
            "asks": [],
            "checksum": 2114181697,
            "timestamp": "2023-10-06T17:35:55.440295Z"
        }
    ]
}
```