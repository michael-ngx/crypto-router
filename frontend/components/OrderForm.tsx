"use client";

import { useState } from "react";
import { useAuth } from "../contexts/AuthContext";

const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ?? "http://localhost:8080";

interface OrderFormProps {
  symbol?: string;
  onOrderCreated?: (orderId: string) => void;
}

export function OrderForm({ symbol = "BTC-USD", onOrderCreated }: OrderFormProps) {
  const { user, isAuthenticated } = useAuth();
  const [side, setSide] = useState<"BUY" | "SELL">("BUY");
  const [orderType, setOrderType] = useState<"MARKET" | "LIMIT">("MARKET");
  const [qty, setQty] = useState("");
  const [price, setPrice] = useState("");
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);
    setSuccess(null);
    setIsSubmitting(true);

    if (!isAuthenticated || !user) {
      setError("Please login to place an order");
      setIsSubmitting(false);
      return;
    }

    try {
      const qtyNum = parseFloat(qty);
      if (isNaN(qtyNum) || qtyNum <= 0) {
        throw new Error("Quantity must be a positive number");
      }

      if (orderType === "LIMIT") {
        const priceNum = parseFloat(price);
        if (isNaN(priceNum) || priceNum <= 0) {
          throw new Error("Limit orders require a valid price");
        }
      }

      const orderData: any = {
        symbol,
        side: side.toLowerCase(),
        type: orderType.toLowerCase(),
        qty: qtyNum,
        user_id: user.user_id,
      };

      // Only include price for limit orders
      if (orderType === "LIMIT") {
        orderData.price = parseFloat(price);
      }

      const response = await fetch(`${API_BASE_URL}/api/orders`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify(orderData),
      });

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({ error: "Unknown error" }));
        throw new Error(errorData.error || `HTTP ${response.status}`);
      }

      const result = await response.json();
      setSuccess(`Order created: ${result.order_id}`);
      
      // Reset form
      setQty("");
      setPrice("");
      
      // Callback if provided
      if (onOrderCreated) {
        onOrderCreated(result.order_id);
      }
    } catch (err: any) {
      setError(err?.message || "Failed to create order");
    } finally {
      setIsSubmitting(false);
    }
  };

  return (
    <form onSubmit={handleSubmit} className="flex flex-col gap-4">
      <div className="flex flex-col gap-2">
        <label className="text-sm text-slate-200">Trading pair</label>
        <input
          type="text"
          value={symbol}
          disabled
          className="rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-300"
        />
      </div>

      <div className="flex gap-4">
        <div className="flex-1">
          <label className="text-sm text-slate-200">Side</label>
          <div className="mt-1 flex gap-2">
            <button
              type="button"
              onClick={() => setSide("BUY")}
              className={`flex-1 rounded-full border px-3 py-1 text-sm font-medium transition-colors ${
                side === "BUY"
                  ? "border-green-500/70 bg-green-500/20 text-slate-50"
                  : "border-slate-700 bg-slate-950 text-slate-300 hover:border-slate-600 hover:text-slate-50"
              }`}
            >
              Buy
            </button>
            <button
              type="button"
              onClick={() => setSide("SELL")}
              className={`flex-1 rounded-full border px-3 py-1 text-sm font-medium transition-colors ${
                side === "SELL"
                  ? "border-red-500/70 bg-red-500/20 text-slate-50"
                  : "border-slate-700 bg-slate-950 text-slate-300 hover:border-slate-600 hover:text-slate-50"
              }`}
            >
              Sell
            </button>
          </div>
        </div>

        <div className="flex-1">
          <label className="text-sm text-slate-200">Order type</label>
          <div className="mt-1 flex gap-2">
            <button
              type="button"
              onClick={() => setOrderType("MARKET")}
              className={`flex-1 rounded-full border px-3 py-1 text-sm font-medium transition-colors ${
                orderType === "MARKET"
                  ? "border-blue-500/70 bg-blue-500/20 text-slate-50"
                  : "border-slate-700 bg-slate-950 text-slate-300 hover:border-slate-600 hover:text-slate-50"
              }`}
            >
              Market
            </button>
            <button
              type="button"
              onClick={() => setOrderType("LIMIT")}
              className={`flex-1 rounded-full border px-3 py-1 text-sm font-medium transition-colors ${
                orderType === "LIMIT"
                  ? "border-blue-500/70 bg-blue-500/20 text-slate-50"
                  : "border-slate-700 bg-slate-950 text-slate-300 hover:border-slate-600 hover:text-slate-50"
              }`}
            >
              Limit
            </button>
          </div>
        </div>
      </div>

      <div className="flex gap-4">
        <div className="flex-1">
          <label className="text-sm text-slate-200">Quantity</label>
          <input
            type="number"
            step="any"
            min="0"
            value={qty}
            onChange={(e) => setQty(e.target.value)}
            required
            className="mt-1 w-full rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
            placeholder="0.00"
          />
        </div>

        {orderType === "LIMIT" && (
          <div className="flex-1">
            <label className="text-sm text-slate-200">Price</label>
            <input
              type="number"
              step="any"
              min="0"
              value={price}
              onChange={(e) => setPrice(e.target.value)}
              required={orderType === "LIMIT"}
              className="mt-1 w-full rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
              placeholder="0.00"
            />
          </div>
        )}
      </div>

      {!isAuthenticated && (
        <div className="rounded-lg border border-yellow-500/40 bg-yellow-950/40 px-3 py-2 text-xs text-yellow-200">
          Please <a href="/login" className="underline">login</a> to place orders
        </div>
      )}

      {error && (
        <div className="rounded-lg border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
          {error}
        </div>
      )}

      {success && (
        <div className="rounded-lg border border-green-500/40 bg-green-950/40 px-3 py-2 text-xs text-green-200">
          {success}
        </div>
      )}

      <button
        type="submit"
        disabled={isSubmitting}
        className={`rounded-full border px-4 py-2 text-sm font-medium transition-colors ${
          side === "BUY"
            ? "border-green-500/70 bg-green-500/20 text-slate-50 hover:bg-green-500/30"
            : "border-red-500/70 bg-red-500/20 text-slate-50 hover:bg-red-500/30"
        } disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:bg-opacity-20`}
      >
        {isSubmitting ? "Submitting..." : `${side} ${symbol.split("-")[0]}`}
      </button>
    </form>
  );
}

