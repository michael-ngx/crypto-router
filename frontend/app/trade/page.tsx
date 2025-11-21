"use client";

import { useEffect, useRef, useState } from "react";
import { useRouter } from "next/navigation";
import { useAuth } from "../../contexts/AuthContext";
import {
  ConsolidatedOrderBook,
  type BookResponse,
} from "../../components/ConsolidatedOrderBook";

const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ?? "http://localhost:8080";

// How long we allow the book to go without a successful update
// before we consider it "stale" and show the loading indicator again.
const STALE_MS = 5000;

export default function TradePage() {
  const { isAuthenticated, isLoading: authLoading } = useAuth();
  const router = useRouter();
  const [selectedPair, setSelectedPair] = useState("BTC-USD");
  const [bookData, setBookData] = useState<BookResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Track time of last successful book update (with at least one level)
  const lastUpdateRef = useRef<number | null>(null);

  // Redirect to home if not authenticated
  useEffect(() => {
    if (!authLoading && !isAuthenticated) {
      router.push("/");
    }
  }, [isAuthenticated, authLoading, router]);

  // Fetch order book data - must be called before any early returns
  useEffect(() => {
    // Don't fetch if not authenticated or still loading
    if (authLoading || !isAuthenticated) {
      return;
    }
    let isCancelled = false;
    let timerId: number | null = null;
    let pollSeq = 0;

    // New symbol or first mount: treat as initial load, no data yet.
    setIsLoading(true);
    setError(null);
    setBookData(null);
    lastUpdateRef.current = null;

    const fetchBook = async () => {
      if (isCancelled) return;

      const now = Date.now();
      const lastUpdate = lastUpdateRef.current;

      // If we had data before and it's stale, go back to a "loading" state:
      // show Updating..., hide table, no error.
      if (lastUpdate !== null && now - lastUpdate > STALE_MS) {
        setIsLoading(true);
        setBookData(null);
        setError(null);
      }

      const currentSeq = ++pollSeq;

      try {
        const url = `${API_BASE_URL}/api/book?symbol=${encodeURIComponent(
          selectedPair
        )}&depth=10`;

        const resp = await fetch(url);
        if (!resp.ok) {
          throw new Error(`HTTP ${resp.status}`);
        }

        const data = (await resp.json()) as BookResponse;

        const hasLevels =
          (data.bids && data.bids.length > 0) ||
          (data.asks && data.asks.length > 0);

        if (!isCancelled && currentSeq === pollSeq && hasLevels) {
          // Valid snapshot: enter READY state
          setBookData(data);
          setIsLoading(false);
          setError(null);
          lastUpdateRef.current = Date.now();
        }
        // If !hasLevels: treat as "no new valid data".
        // We keep whatever state we were already in (loading or ready),
        // and let stale logic handle it if it goes too long.
      } catch (err: any) {
        if (!isCancelled) {
          // Error state: we keep polling but show "Updating..." + error,
          // and invalidate the table.
          setIsLoading(true);
          setBookData(null);
          setError(err?.message ?? "Failed to fetch book");
          // lastUpdateRef.current remains unchanged: tracks last success.
        }
      } finally {
        if (!isCancelled) {
          timerId = window.setTimeout(fetchBook, 1000); // Re-run after 1s
        }
      }
    };

    fetchBook();

    return () => {
      isCancelled = true;
      if (timerId !== null) {
        clearTimeout(timerId);
      }
    };
  }, [selectedPair, authLoading, isAuthenticated]);

  if (authLoading || !isAuthenticated) {
    return (
      <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
        <div className="text-slate-400">Loading...</div>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-6">
      <section className="flex flex-wrap items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-semibold text-slate-50">Trade</h2>
          <p className="text-xs text-slate-400">
            Consolidated order book across multiple exchanges
          </p>
          {isLoading && (
            <p className="mt-1 text-[11px] text-slate-500">Updating…</p>
          )}
        </div>

        <div className="flex items-center gap-3">
          <label className="text-sm text-slate-200">Trading pair</label>
          <select
            value={selectedPair}
            onChange={(e) => setSelectedPair(e.target.value)}
            className="rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
          >
            <option value="BTC-USD">BTC / USD</option>
            {/* TODO: dynamically add available pairs here */}
          </select>
        </div>
      </section>

      {error && (
        <div className="rounded-lg border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
          Error fetching book: {error}
        </div>
      )}

      {/* Enforce mutual exclusivity: table only when we have valid data */}
      {bookData && !isLoading ? (
        <ConsolidatedOrderBook book={bookData} lastUpdated={lastUpdateRef.current} />
      ) : null}
    </div>
  );
}