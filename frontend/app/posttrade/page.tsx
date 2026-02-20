"use client";

import { useEffect, useState, useCallback } from "react";
import { useRouter } from "next/navigation";
import { useAuth } from "../../contexts/AuthContext";

const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ?? "http://localhost:8080";

interface Order {
  id: string;
  symbol: string;
  side: string;
  order_type: string;
  quantity_requested: number;
  limit_price: number | null;
  quantity_planned: number;
  price_planned_avg: number;
  fully_routable: boolean;
  routing_message: string | null;
  quantity_filled: number;
  price_filled_avg: number | null;
  status: string;
  created_at: string;
  execution_started_at: string | null;
  terminal_at: string | null;
  last_updated_at: string | null;
}

export default function PostTradePage() {
  const { isAuthenticated, isLoading: authLoading, user } = useAuth();
  const router = useRouter();
  const [orders, setOrders] = useState<Order[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [cancellingOrderId, setCancellingOrderId] = useState<string | null>(null);

  // Redirect to home if not authenticated
  useEffect(() => {
    if (!authLoading && !isAuthenticated) {
      router.push("/");
    }
  }, [isAuthenticated, authLoading, router]);

  // Fetch orders function
  const fetchOrders = useCallback(async () => {
    if (!user) return;

    setIsLoading(true);
    setError(null);

    try {
      const response = await fetch(
        `${API_BASE_URL}/api/orders?user_id=${encodeURIComponent(user.user_id)}`
      );

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const data = await response.json();
      setOrders(data.orders || []);
    } catch (err: any) {
      setError(err?.message || "Failed to fetch orders");
    } finally {
      setIsLoading(false);
    }
  }, [user]);

  // Fetch orders for the current user
  useEffect(() => {
    if (!isAuthenticated || !user || authLoading) {
      return;
    }

    fetchOrders();

    // Refresh orders every 5 seconds
    const interval = setInterval(fetchOrders, 5000);
    return () => clearInterval(interval);
  }, [isAuthenticated, user, authLoading, fetchOrders]);

  const handleCancelOrder = async (orderId: string) => {
    if (!user) return;

    setCancellingOrderId(orderId);
    setError(null);

    try {
      const response = await fetch(`${API_BASE_URL}/api/orders/${orderId}`, {
        method: "PATCH",
      });

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.error || `HTTP ${response.status}`);
      }

      // Refresh orders after successful cancellation
      await fetchOrders();
    } catch (err: any) {
      setError(err?.message || "Failed to cancel order");
    } finally {
      setCancellingOrderId(null);
    }
  };

  if (authLoading || !isAuthenticated) {
    return (
      <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
        <div className="text-slate-400">Loading...</div>
      </div>
    );
  }

  const getStatusColor = (status: string) => {
    switch (status) {
      case "open":
        return "text-yellow-400";
      case "executing":
        return "text-cyan-400";
      case "partially_filled":
        return "text-blue-400";
      case "filled":
        return "text-green-400";
      case "failed":
        return "text-rose-400";
      case "expired":
        return "text-orange-400";
      case "cancelled":
        return "text-red-400";
      default:
        return "text-slate-400";
    }
  };

  const getStatusLabel = (status: string) => {
    switch (status) {
      case "open":
        return "Open";
      case "executing":
        return "Executing";
      case "partially_filled":
        return "Partially Filled";
      case "filled":
        return "Filled";
      case "failed":
        return "Failed";
      case "expired":
        return "Expired";
      case "cancelled":
        return "Cancelled";
      default:
        return status;
    }
  };

  const openOrders = orders.filter(
    (order) =>
      order.status === "open" ||
      order.status === "executing" ||
      order.status === "partially_filled"
  );

  return (
    <div className="flex flex-col gap-6">
      <div>
        <h2 className="text-xl font-semibold text-slate-50">
          Analytics dashboard
        </h2>
        <p className="text-xs text-slate-400">
          View your order history and execution details
        </p>
      </div>

      {error && (
        <div className="rounded-lg border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
          {error}
        </div>
      )}

      {/* Pending Orders Section */}
      <section>
        <div className="mb-4 flex items-center justify-between">
          <div>
            <h3 className="text-lg font-semibold text-slate-50">
              Pending Orders
            </h3>
            <p className="text-xs text-slate-400">
              Orders that are currently open, executing, or partially filled
            </p>
          </div>
          {isLoading && (
            <p className="text-[11px] text-slate-500">Updating…</p>
          )}
        </div>

        {openOrders.length === 0 ? (
          <div className="rounded-lg border border-slate-700 bg-slate-900/50 p-8 text-center">
            <p className="text-sm text-slate-400">No pending orders</p>
          </div>
        ) : (
          <div className="overflow-x-auto rounded-lg border border-slate-700 bg-slate-900/50">
            <table className="w-full">
              <thead className="border-b border-slate-700 bg-slate-900/70">
                <tr>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Symbol
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Side
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Type
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Quantity Requested
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Limit Price
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Status
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Created
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Actions
                  </th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800">
                {openOrders.map((order) => (
                  <tr key={order.id} className="hover:bg-slate-800/50">
                    <td className="px-4 py-3 text-sm text-slate-100">
                      {order.symbol}
                    </td>
                    <td className="px-4 py-3">
                      <span
                        className={`text-sm font-medium ${
                          order.side === "buy"
                            ? "text-green-400"
                            : "text-red-400"
                        }`}
                      >
                        {order.side.toUpperCase()}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-300">
                      {order.order_type.charAt(0).toUpperCase() +
                        order.order_type.slice(1)}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-100">
                      {order.quantity_requested.toLocaleString(undefined, {
                        maximumFractionDigits: 8,
                      })}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-300">
                      {order.limit_price != null
                        ? order.limit_price.toLocaleString(undefined, {
                            maximumFractionDigits: 2,
                          })
                        : "—"}
                    </td>
                    <td className="px-4 py-3">
                      <span
                        className={`text-xs font-medium ${getStatusColor(
                          order.status
                        )}`}
                      >
                        {getStatusLabel(order.status)}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-400">
                      {new Date(order.created_at).toLocaleString()}
                    </td>
                    <td className="px-4 py-3">
                      <button
                        onClick={() => handleCancelOrder(order.id)}
                        disabled={cancellingOrderId === order.id}
                        className="rounded-full border border-red-500/40 bg-red-950/40 px-3 py-1.5 text-xs font-medium text-red-200 transition-colors hover:bg-red-950/60 disabled:opacity-50 disabled:cursor-not-allowed"
                      >
                        {cancellingOrderId === order.id ? "Cancelling..." : "Cancel"}
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>

      {/* All Orders Section */}
      {orders.length > 0 && (
        <section>
          <div className="mb-4">
            <h3 className="text-lg font-semibold text-slate-50">
              All Orders
            </h3>
            <p className="text-xs text-slate-400">
              Complete order history ({orders.length} total)
            </p>
          </div>

          <div className="overflow-x-auto rounded-lg border border-slate-700 bg-slate-900/50">
            <table className="w-full">
              <thead className="border-b border-slate-700 bg-slate-900/70">
                <tr>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Symbol
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Side
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Type
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Quantity Requested
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Quantity Filled
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Limit Price
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Planned Price Avg
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Avg Fill Price
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Status
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Created
                  </th>
                  <th className="px-4 py-3 text-left text-xs font-medium text-slate-300">
                    Terminal
                  </th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800">
                {orders.map((order) => (
                  <tr key={order.id} className="hover:bg-slate-800/50">
                    <td className="px-4 py-3 text-sm text-slate-100">
                      {order.symbol}
                    </td>
                    <td className="px-4 py-3">
                      <span
                        className={`text-sm font-medium ${
                          order.side === "buy"
                            ? "text-green-400"
                            : "text-red-400"
                        }`}
                      >
                        {order.side.toUpperCase()}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-300">
                      {order.order_type.charAt(0).toUpperCase() +
                        order.order_type.slice(1)}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-100">
                      {order.quantity_requested.toLocaleString(undefined, {
                        maximumFractionDigits: 8,
                      })}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-100">
                      {order.quantity_filled.toLocaleString(undefined, {
                        maximumFractionDigits: 8,
                      })}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-300">
                      {order.limit_price != null
                        ? order.limit_price.toLocaleString(undefined, {
                            maximumFractionDigits: 2,
                          })
                        : "—"}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-300">
                      {order.price_planned_avg.toLocaleString(undefined, {
                        maximumFractionDigits: 2,
                      })}
                    </td>
                    <td className="px-4 py-3 text-sm text-slate-300">
                      {order.price_filled_avg != null
                        ? order.price_filled_avg.toLocaleString(undefined, {
                            maximumFractionDigits: 2,
                          })
                        : "—"}
                    </td>
                    <td className="px-4 py-3">
                      <span
                        className={`text-xs font-medium ${getStatusColor(
                          order.status
                        )}`}
                      >
                        {getStatusLabel(order.status)}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-400">
                      {new Date(order.created_at).toLocaleString()}
                    </td>
                    <td className="px-4 py-3 text-xs text-slate-400">
                      {order.terminal_at
                        ? new Date(order.terminal_at).toLocaleString()
                        : "—"}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      )}
    </div>
  );
}
