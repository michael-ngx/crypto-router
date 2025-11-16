"use client";

import { useEffect, useState } from "react";

type BookResponse = unknown; // later you can replace this with a proper type

const API_BASE_URL = process.env.NEXT_PUBLIC_API_BASE_URL ?? "http://localhost:8080";

export default function TradePage() {
  const [selectedPair, setSelectedPair] = useState("BTC-USD");
  const [bookData, setBookData] = useState<BookResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let isCancelled = false;
    let timerId: number | null = null;
    let pollSeq = 0;

    const fetchBook = async () => {
      if (isCancelled) return;

      setIsLoading(true);
      setError(null);
      const currentSeq = ++pollSeq;

      try {
        const url = `${API_BASE_URL}/api/book?symbol=${encodeURIComponent(
          selectedPair
        )}&depth=10`;

        const resp = await fetch(url);
        if (!resp.ok) {
          throw new Error(`HTTP ${resp.status}`);
        }

        // If you are still debugging JSON issues, you can switch to resp.text()
        const data = (await resp.json()) as BookResponse;

        if (!isCancelled && currentSeq === pollSeq) {
          setBookData(data);
        }
      } catch (err: any) {
        if (!isCancelled) {
          setError(err?.message ?? "Failed to fetch book");
        }
      } finally {
        if (!isCancelled) {
          setIsLoading(false);
          timerId = window.setTimeout(fetchBook, 1000);
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
  }, [selectedPair]);

  return (
    <div className="flex flex-col gap-6">
      <section className="flex flex-wrap items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-semibold text-slate-50">Trade</h2>
          <p className="text-xs text-slate-400">
            Experimental UI calling the router backend book API every second.
          </p>
        </div>

        <div className="flex items-center gap-3">
          <label className="text-sm text-slate-200">Trading pair</label>
          <select
            value={selectedPair}
            onChange={(e) => setSelectedPair(e.target.value)}
            className="rounded-full border border-slate-700 bg-slate-950 px-3 py-1 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
          >
            <option value="BTC-USD">BTC / USD</option>
          </select>
        </div>
      </section>

      <section className="rounded-xl border border-slate-700 bg-slate-900/90 px-4 py-3">
        <div className="mb-3 flex items-center justify-between">
          <span className="text-sm text-slate-100">
            Raw book API response (depth = 10)
          </span>
          <span className="text-xs text-slate-400">
            Polling every 1 second
            {isLoading ? " · loading…" : ""}
          </span>
        </div>

        {error && (
          <div className="mb-3 text-xs text-red-300">Error: {error}</div>
        )}

        <pre className="m-0 whitespace-pre-wrap break-words text-xs font-mono text-slate-100">
          {bookData
            ? JSON.stringify(bookData, null, 2)
            : "No data yet. Waiting for first response…"}
        </pre>
      </section>
    </div>
  );
}