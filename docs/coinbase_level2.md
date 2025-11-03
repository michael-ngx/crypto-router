## Coinbase Advanced Trade API

[Link](https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/websocket/websocket-channels#level2-channel)

The level2 channel sends a message with fields, `type` (“snapshot” or “update”), `product_id`, and `updates`. The field `updates` is an array of objects of {`price_level`, `new_quantity`, `event_time`, `side`} to represent the entire order book. The `event_time` property is the time of the event as recorded by our trading engine.

The `new_quantity` property is the updated size at that price level, not a delta. A `new_quantity` of “0” indicates the price level can be removed.


```json
{
  "channel": "l2_data",
  "client_id": "",
  "timestamp": "2023-02-09T20:32:50.714964855Z",
  "sequence_num": 0,
  "events": [
    {
      "type": "snapshot",
      "product_id": "BTC-USD",
      "updates": [
        {
          "side": "bid",
          "event_time": "1970-01-01T00:00:00Z",
          "price_level": "21921.73",
          "new_quantity": "0.06317902"
        },
        {
          "side": "bid",
          "event_time": "1970-01-01T00:00:00Z",
          "price_level": "21921.3",
          "new_quantity": "0.02"
        }
      ]
    }
  ]
}
```