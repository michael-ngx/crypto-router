
What happens now after routing path is calculated

- RouterService computes an indicative route plan from live books via `route_order_from_books(...)` in `router_service.hpp (line 66)`
- For market orders, if no quantity is currently routable, it returns MarketNoLiquidity and the request fails early in `router_service.hpp (line 77)`.
- Otherwise, order is persisted as open only (no fill completion claim) in `router_service.hpp (line 87)`.
- orders.average_fill_price and orders.closed_at are not set during planning stage (execution not performed yet) in `router_service.hpp (line 97)`.
- order_breakdown is not written during planning stage (since no real exchange fills yet) in `router_service.hpp (line 123)`.
- HTTP response returns indicative routing fields (routable_qty, indicative_average_price) in `http_routes.hpp (line 380)`.