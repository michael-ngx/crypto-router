"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import { useRouter } from "next/navigation";
import { useAuth } from "../../contexts/AuthContext";
import {
  ConsolidatedOrderBook,
  type BookResponse,
} from "../../components/ConsolidatedOrderBook";
import { OrderForm } from "../../components/OrderForm";
import { API_BASE_URL } from "@/lib/api";

// Parse "BASE-QUOTE" pairs into base -> quotes map; bases and quotes sorted.
function useBaseQuoteFromPairs(pairs: string[]) {
  return useMemo(() => {
    const baseToQuotes: Record<string, string[]> = {};
    for (const pair of pairs) {
      const idx = pair.indexOf("-");
      if (idx <= 0 || idx === pair.length - 1) continue;
      const base = pair.slice(0, idx);
      const quote = pair.slice(idx + 1);
      if (!baseToQuotes[base]) baseToQuotes[base] = [];
      if (!baseToQuotes[base].includes(quote)) baseToQuotes[base].push(quote);
    }
    const bases = Object.keys(baseToQuotes).sort();
    for (const base of bases) {
      baseToQuotes[base].sort();
    }
    return { baseToQuotes, bases };
  }, [pairs]);
}

export default function TradePage() {
  const { isAuthenticated, isLoading: authLoading } = useAuth();
  const router = useRouter();
  const [availablePairs, setAvailablePairs] = useState<string[]>([]);
  const [pairsLoading, setPairsLoading] = useState(false);
  const [pairsError, setPairsError] = useState<string | null>(null);
  const [selectedBase, setSelectedBase] = useState("");
  const [selectedQuote, setSelectedQuote] = useState("");
  const [bookData, setBookData] = useState<BookResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const { baseToQuotes, bases } = useBaseQuoteFromPairs(availablePairs);
  const quoteOptions = selectedBase ? baseToQuotes[selectedBase] ?? [] : [];
  const selectedPair =
    selectedBase && selectedQuote && quoteOptions.includes(selectedQuote)
      ? `${selectedBase}-${selectedQuote}`
      : "";

  // Track time of last successful book update (with at least one level)
  const lastUpdateRef = useRef<number | null>(null);

  const invalidateBook = ({
    loading = true,
    resetLastUpdate = true,
    clearError = true,
  }: {
    loading?: boolean;
    resetLastUpdate?: boolean;
    clearError?: boolean;
  } = {}) => {
    setIsLoading(loading);
    setBookData(null);
    if (clearError) {
      setError(null);
    }
    if (resetLastUpdate) {
      lastUpdateRef.current = null;
    }
  };

  // Redirect to home if not authenticated
  useEffect(() => {
    if (!authLoading && !isAuthenticated) {
      router.push("/");
    }
  }, [isAuthenticated, authLoading, router]);

  // Fetch available trading pairs from backend
  useEffect(() => {
    if (authLoading || !isAuthenticated) {
      return;
    }

    let isCancelled = false;
    setPairsLoading(true);
    setPairsError(null);

    const fetchPairs = async () => {
      try {
        const resp = await fetch(`${API_BASE_URL}/api/pairs`);
        if (!resp.ok) {
          throw new Error(`HTTP ${resp.status}`);
        }

        const data = await resp.json();
        const pairs = Array.isArray(data?.pairs)
          ? data.pairs.filter(
              (pair: unknown): pair is string =>
                typeof pair === "string" && pair.length > 0
            )
          : [];

        if (!isCancelled) {
          setAvailablePairs(pairs);
          if (pairs.length > 0) {
            const { baseToQuotes, bases } = (() => {
              const b2q: Record<string, string[]> = {};
              for (const pair of pairs) {
                const idx = pair.indexOf("-");
                if (idx <= 0 || idx === pair.length - 1) continue;
                const base = pair.slice(0, idx);
                const quote = pair.slice(idx + 1);
                if (!b2q[base]) b2q[base] = [];
                if (!b2q[base].includes(quote)) b2q[base].push(quote);
              }
              const b = Object.keys(b2q).sort();
              b.forEach((base) => b2q[base].sort());
              return { baseToQuotes: b2q, bases: b };
            })();
            const base = bases.includes("BTC") ? "BTC" : bases[0];
            const quotes = baseToQuotes[base] ?? [];
            const quote = quotes.includes("USD") ? "USD" : quotes[0] ?? "";
            setSelectedBase(base);
            setSelectedQuote(quote);
          } else {
            setSelectedBase("");
            setSelectedQuote("");
          }
        }
      } catch (err: any) {
        if (!isCancelled) {
          setPairsError(err?.message ?? "Failed to fetch pairs");
        }
      } finally {
        if (!isCancelled) {
          setPairsLoading(false);
        }
      }
    };

    fetchPairs();

    return () => {
      isCancelled = true;
    };
  }, [authLoading, isAuthenticated]);

  // Fetch order book data - must be called before any early returns
  useEffect(() => {
    // Don't fetch if not authenticated or still loading
    if (authLoading || !isAuthenticated) {
      return;
    }

    if (!selectedPair) {
      invalidateBook({ loading: false });
      return;
    }

    if (availablePairs.length > 0 && !availablePairs.includes(selectedPair)) {
      return;
    }

    let isCancelled = false;
    let timerId: number | null = null;
    let pollSeq = 0;

    // New symbol or first mount: treat as initial load, no data yet.
    invalidateBook();

    const fetchBook = async () => {
      if (isCancelled) return;

      const currentSeq = ++pollSeq;

      try {
        const url = `${API_BASE_URL}/api/book?symbol=${encodeURIComponent(
          selectedPair
        )}&depth=24`;

        const resp = await fetch(url);
        let data: BookResponse | null = null;

        try {
          data = (await resp.json()) as BookResponse;
        } catch {
          data = null;
        }

        if (!resp.ok) {
          const msg =
            data?.status?.message ??
            (data?.status?.code ? `HTTP ${data.status.code}` : `HTTP ${resp.status}`);
          throw new Error(msg);
        }

        const statusCode = data?.status?.code ?? 200;

        if (!isCancelled && currentSeq === pollSeq && statusCode !== 200) {
          invalidateBook();
          setError(data?.status?.message ?? "Market data unavailable");
          return;
        }

        const hasLevels =
          (data?.bids && data.bids.length > 0) ||
          (data?.asks && data.asks.length > 0);

        if (!isCancelled && currentSeq === pollSeq) {
          // Always update bookData on success so UI can show status message
          setBookData(data);
          setError(null);
          if (hasLevels) {
            setIsLoading(false);
            lastUpdateRef.current = data?.last_updated_ms ?? null;
          }
          // If !hasLevels: keep loading state, UI will show "Connecting..."
        }
      } catch (err: any) {
        if (!isCancelled) {
          // Error state: we keep polling but show "Updating..." + error,
          // and invalidate the table.
          invalidateBook({ resetLastUpdate: false, clearError: false });
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
  }, [selectedPair, authLoading, isAuthenticated, availablePairs]);

  if (authLoading || !isAuthenticated) {
    return (
      <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
        <div className="text-slate-400">Loading...</div>
      </div>
    );
  }

  const displayBook =
    selectedPair && bookData?.symbol === selectedPair ? bookData : null;

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

        {!pairsError ? (
          <div className="flex flex-wrap items-center gap-3">
            <div className="flex items-center gap-2">
              <label className="text-sm text-slate-200">Base</label>
              <select
                value={selectedBase}
                onChange={(e) => {
                  const newBase = e.target.value;
                  setSelectedBase(newBase);
                  const quotes = baseToQuotes[newBase] ?? [];
                  setSelectedQuote(
                    quotes.includes(selectedQuote) ? selectedQuote : quotes[0] ?? ""
                  );
                  invalidateBook();
                }}
                disabled={pairsLoading || bases.length === 0}
                className="rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
              >
                {pairsLoading ? (
                  <option value="">Loading...</option>
                ) : bases.length === 0 ? (
                  <option value="">No bases</option>
                ) : (
                  bases.map((base) => (
                    <option key={base} value={base}>
                      {base}
                    </option>
                  ))
                )}
              </select>
            </div>
            <div className="flex items-center gap-2">
              <label className="text-sm text-slate-200">Quote</label>
              <select
                value={selectedQuote}
                onChange={(e) => {
                  setSelectedQuote(e.target.value);
                  invalidateBook();
                }}
                disabled={
                  pairsLoading || !selectedBase || quoteOptions.length === 0
                }
                className="rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
              >
                {!selectedBase ? (
                  <option value="">Select base first</option>
                ) : quoteOptions.length === 0 ? (
                  <option value="">No quotes</option>
                ) : (
                  quoteOptions.map((quote) => (
                    <option key={quote} value={quote}>
                      {quote}
                    </option>
                  ))
                )}
              </select>
            </div>
            {selectedPair && (
              <span className="text-sm text-slate-400">
                {selectedBase} / {selectedQuote}
              </span>
            )}
          </div>
        ) : null}
        {pairsError && (
          <p className="text-[11px] text-red-400">Failed to load pairs: {pairsError}</p>
        )}
      </section>

      {error && (
        <div className="rounded-lg border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
          Error fetching book: {error}
        </div>
      )}

      {/* Order book: show when we have a pair and either data or a response to display */}
      {selectedPair && (bookData || isLoading) ? (
        <ConsolidatedOrderBook
          book={displayBook}
          lastUpdated={lastUpdateRef.current}
          statusMessage={
            displayBook?.status?.message ??
            (isLoading ? "Connecting to venues…" : null)
          }
        />
      ) : null}

      {/* Order Form */}
      <section className="rounded-lg border border-slate-700 bg-slate-900/50 p-6">
        <h2 className="mb-4 text-lg font-semibold text-slate-50">Place Order</h2>
        {selectedPair ? (
          <OrderForm symbol={selectedPair} />
        ) : (
          <p className="text-sm text-slate-400">No trading pairs available.</p>
        )}
      </section>
    </div>
  );
}
