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

      const orderData = {
        symbol,
        side,
        type: orderType,
        qty: qtyNum,
        price: orderType === "LIMIT" ? parseFloat(price) : 0,
        user_id: user.user_id,
      };

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
        <label className="text-sm font-medium text-slate-200">Trading Pair</label>
        <input
          type="text"
          value={symbol}
          disabled
          className="rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-300"
        />
      </div>

      <div className="flex gap-4">
        <div className="flex-1">
          <label className="text-sm font-medium text-slate-200">Side</label>
          <div className="mt-1 flex gap-2">
            <button
              type="button"
              onClick={() => setSide("BUY")}
              className={`flex-1 rounded-lg px-4 py-2 text-sm font-medium transition-colors ${
                side === "BUY"
                  ? "bg-green-600 text-white"
                  : "bg-slate-800 text-slate-300 hover:bg-slate-700"
              }`}
            >
              Buy
            </button>
            <button
              type="button"
              onClick={() => setSide("SELL")}
              className={`flex-1 rounded-lg px-4 py-2 text-sm font-medium transition-colors ${
                side === "SELL"
                  ? "bg-red-600 text-white"
                  : "bg-slate-800 text-slate-300 hover:bg-slate-700"
              }`}
            >
              Sell
            </button>
          </div>
        </div>

        <div className="flex-1">
          <label className="text-sm font-medium text-slate-200">Order Type</label>
          <div className="mt-1 flex gap-2">
            <button
              type="button"
              onClick={() => setOrderType("MARKET")}
              className={`flex-1 rounded-lg px-4 py-2 text-sm font-medium transition-colors ${
                orderType === "MARKET"
                  ? "bg-blue-600 text-white"
                  : "bg-slate-800 text-slate-300 hover:bg-slate-700"
              }`}
            >
              Market
            </button>
            <button
              type="button"
              onClick={() => setOrderType("LIMIT")}
              className={`flex-1 rounded-lg px-4 py-2 text-sm font-medium transition-colors ${
                orderType === "LIMIT"
                  ? "bg-blue-600 text-white"
                  : "bg-slate-800 text-slate-300 hover:bg-slate-700"
              }`}
            >
              Limit
            </button>
          </div>
        </div>
      </div>

      <div className="flex gap-4">
        <div className="flex-1">
          <label className="text-sm font-medium text-slate-200">
            Quantity
          </label>
          <input
            type="number"
            step="any"
            min="0"
            value={qty}
            onChange={(e) => setQty(e.target.value)}
            required
            className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
            placeholder="0.00"
          />
        </div>

        {orderType === "LIMIT" && (
          <div className="flex-1">
            <label className="text-sm font-medium text-slate-200">
              Price
            </label>
            <input
              type="number"
              step="any"
              min="0"
              value={price}
              onChange={(e) => setPrice(e.target.value)}
              required={orderType === "LIMIT"}
              className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
              placeholder="0.00"
            />
          </div>
        )}
      </div>

      {!isAuthenticated && (
        <div className="rounded-lg border border-yellow-500/40 bg-yellow-950/40 px-3 py-2 text-sm text-yellow-200">
          Please <a href="/login" className="underline">login</a> to place orders
        </div>
      )}

      {error && (
        <div className="rounded-lg border border-red-500/40 bg-red-950/40 px-3 py-2 text-sm text-red-200">
          {error}
        </div>
      )}

      {success && (
        <div className="rounded-lg border border-green-500/40 bg-green-950/40 px-3 py-2 text-sm text-green-200">
          {success}
        </div>
      )}

      <button
        type="submit"
        disabled={isSubmitting}
        className={`rounded-lg px-4 py-2 font-medium text-white transition-colors ${
          side === "BUY"
            ? "bg-green-600 hover:bg-green-700"
            : "bg-red-600 hover:bg-red-700"
        } disabled:opacity-50 disabled:cursor-not-allowed`}
      >
        {isSubmitting ? "Submitting..." : `${side} ${symbol.split("-")[0]}`}
      </button>
    </form>
  );
}

