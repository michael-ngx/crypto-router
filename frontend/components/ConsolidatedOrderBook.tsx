"use client";

import { useMemo, useState } from "react";
import { formatDynamicPrice } from "@/lib/priceFormat";

export type OrderLevel = {
  price: number;
  size: number;
  venue: string;
};

export type BookResponse = {
  status?: {
    code: number;
    message: string;
  };
  last_updated_ms?: number | null;
  symbol: string;
  venues?: string[];
  bids: OrderLevel[];
  asks: OrderLevel[];
};

type NormalizedBook = {
  symbol: string;
  bids: OrderLevel[];
  asks: OrderLevel[];
};

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Helpers /////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// Convert raw book data (with possible string numbers) into normalized format
function normalizeBook(raw: BookResponse | null): NormalizedBook | null {
  if (!raw) return null;

  const normalizeLevels = (levels: OrderLevel[]): OrderLevel[] =>
    (levels ?? [])
      .map((lvl) => ({
        price: Number(lvl.price),
        size: Number(lvl.size),
        venue: lvl.venue,
      }))
      .filter(
        (l) => Number.isFinite(l.price) && Number.isFinite(l.size)
      );

  return {
    symbol: raw.symbol ?? "UNKNOWN",
    bids: normalizeLevels(raw.bids || []),
    asks: normalizeLevels(raw.asks || []),
  };
}

// Compute scale for order size bars
// X is 1, 10, 100, 1000, ... based on max volume
function computeScale(maxSize: number): number {
  if (!isFinite(maxSize) || maxSize <= 0) return 1;
  const exponent = Math.max(0, Math.ceil(Math.log10(maxSize)));
  return Math.pow(10, exponent);
}

// Format size display number with dynamic decimal places
function formatSize(x: number): string {
  if (x === 0) return "0";

  const abs = Math.abs(x);
  // Decide how many decimals based on magnitude
  let decimals = 3;
  if (abs < 1) decimals = 6;
  if (abs < 0.001) decimals = 8; // very small sizes, more precision

  const s = x.toFixed(decimals);
  // Trim trailing zeros and possible trailing dot
  return s.replace(/0+$/, "").replace(/\.$/, "");
}

// Get badge color classes based on venue name
function venueBadgeColor(venue: string): string {

  const lookupDict: { [key: string]: string } = {
    kraken: "bg-purple-500 text-white",
    coinbase: "bg-blue-500 text-white",
    binance: "bg-yellow-400 text-slate-900",
    okx: "bg-emerald-500 text-white",
  };

  const v = venue.toLowerCase();
  return lookupDict[v] || "bg-slate-700 text-white";
}

// Parse symbol into base and quote currencies
function parseSymbol(sym: string): { base: string; quote: string } {
  if (!sym) return { base: "BASE", quote: "QUOTE" };
  const parts = sym.split(/[-/]/);
  if (parts.length >= 2) {
    return { base: parts[0], quote: parts[1] };
  }
  return { base: sym, quote: "" };
}


/////////////////////////////////////////////////////////////////////////////
///////////////////////////////// Component /////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
type Props = {
  book: BookResponse | null;
  lastUpdated: number | null;
  statusMessage?: string | null;
};

export function ConsolidatedOrderBook({
  book,
  lastUpdated,
  statusMessage,
}: Props) {
  const normalized = normalizeBook(book);

  if (!normalized || (normalized.bids.length === 0 && normalized.asks.length === 0)) {
    return (
      <div className="rounded-xl border border-slate-700 bg-slate-900/90 px-4 py-3 text-sm text-slate-400">
        {statusMessage ?? "No book data yet."}
      </div>
    );
  }

  const { bids, asks } = normalized;
  const { base, quote } = parseSymbol(normalized.symbol);

  const [selectedVenue, setSelectedVenue] = useState<string>("ALL");

  const allVenues = useMemo(() => {
    const venues = new Set<string>();
    bids.forEach((b) => b.venue && venues.add(b.venue));
    asks.forEach((a) => a.venue && venues.add(a.venue));
    return Array.from(venues).sort((a, b) =>
      a.toLowerCase().localeCompare(b.toLowerCase())
    );
  }, [bids, asks]);

  const filtered = useMemo(() => {
    if (selectedVenue === "ALL") {
      return { bids, asks };
    }
    return {
      bids: bids.filter((b) => b.venue === selectedVenue),
      asks: asks.filter((a) => a.venue === selectedVenue),
    };
  }, [bids, asks, selectedVenue]);

  const effectiveBids = filtered.bids;
  const effectiveAsks = filtered.asks;

  const SIDE_ROWS = 6;
  const SIDE_ROWS_WITH_OVERLAP = SIDE_ROWS + 1;

  // Bids panel: highest -> lowest (top -> bottom) and we keep one extra
  // element so we can extract a single overlap level.
  const bidsBestAll = [...effectiveBids]
    .sort((a, b) => b.price - a.price)
    .slice(0, SIDE_ROWS_WITH_OVERLAP);

  // Asks panel uses the lowest asks closest to market, but displayed
  // high->low (reverse) so the "closest" ask is at the bottom.
  const asksBestLowAll = [...effectiveAsks]
    .sort((a, b) => a.price - b.price)
    .slice(0, SIDE_ROWS_WITH_OVERLAP);

  const bestBid = bidsBestAll.length ? bidsBestAll[0].price : null; // highest bid
  const bestAsk = asksBestLowAll.length ? asksBestLowAll[0].price : null; // lowest ask
  const overlapExists =
    bestBid !== null && bestAsk !== null && bestBid >= bestAsk;

  // Single overlap extraction for display.
  const overlapBid = overlapExists ? bidsBestAll[0] ?? null : null;
  const overlapAsk = overlapExists ? asksBestLowAll[0] ?? null : null;

  // Extract exactly one overlapping bid+ask into the overlap section,
  // and remove them from the top/bottom panels.
  const bidsBest = overlapBid ? bidsBestAll.slice(1) : bidsBestAll;
  const asksBestLow = overlapAsk ? asksBestLowAll.slice(1) : asksBestLowAll;
  const asksDisplay = asksBestLow.slice().reverse();

  const maxSizeAcross = Math.max(
    ...bidsBest.map((b) => b.size),
    ...asksBestLow.map((a) => a.size),
    0
  );
  const scale = computeScale(maxSizeAcross);

  const badgeBase = "inline-flex items-center rounded-full px-2 py-0.5 text-[10px] font-semibold";

  return (
    <div className="rounded-xl border border-slate-800 bg-slate-950 px-4 py-3">
      <div className="mb-3 flex items-center justify-between gap-3 text-[11px] text-slate-300">
        <div className="font-semibold uppercase tracking-wide text-slate-400">
          Order Book
        </div>
        {allVenues.length > 0 && (
          <div className="flex items-center gap-2">
            <span className="text-[11px] text-slate-400">Exchange</span>
            <select
              value={selectedVenue}
              onChange={(e) => setSelectedVenue(e.target.value)}
              className="rounded-full border border-slate-700 bg-slate-950 px-2 py-1 text-[11px] text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
            >
              <option value="ALL">All exchanges</option>
              {allVenues.map((venue) => (
                <option key={venue} value={venue}>
                  {venue}
                </option>
              ))}
            </select>
          </div>
        )}
      </div>

      <div className="overflow-hidden rounded-lg border border-slate-800 bg-slate-900/90">
        <div className="flex flex-col">
          {/* Asks panel (top) */}
          <div className="border-b border-slate-800/30 bg-slate-900/30">
            <div className="w-full">
              <div className="px-3 py-2 text-right text-[11px] font-semibold text-slate-300">
                Asks ({base})
              </div>
              <div className="flex flex-col">
                {Array.from({ length: SIDE_ROWS }).map((_, i) => {
                  const ask = asksDisplay[i];
                  const askWidth =
                    ask && scale > 0
                      ? Math.min(100, (ask.size / scale) * 100)
                      : 0;

                  const isOverlapRow =
                    overlapExists && ask && bestBid !== null && ask.price <= bestBid;

                  return (
                    <div
                      key={`ask-${i}`}
                      className={[
                        "flex w-full items-stretch",
                        i % 2 === 0 ? "bg-slate-950/40" : "bg-slate-900/40",
                        isOverlapRow ? "bg-amber-900/20" : "",
                      ].join(" ")}
                      style={{ minHeight: 24 }}
                    >
                      <div className="flex-1" />

                      <div className="w-1/2 flex items-center gap-2 px-3 py-1.5">
                        <div className="w-24 text-left text-[11px] text-red-500 font-medium">
                          {ask ? formatDynamicPrice(ask.price) : ""}
                        </div>

                        <div className="flex-1">
                          {ask ? (
                            <div className="relative h-5 w-full bg-red-900/20 rounded-sm">
                              <div
                                className="absolute inset-y-0 right-0 bg-red-700/40 rounded-sm"
                                style={{ width: `${askWidth}%` }}
                              />
                              <div className="relative z-10 flex h-full items-center justify-start pl-1 text-[11px] text-slate-200">
                                {formatSize(ask.size)}
                              </div>
                            </div>
                          ) : null}
                        </div>

                        <div className="min-w-[62px] text-right">
                          {ask?.venue ? (
                            <span className={`${badgeBase} ${venueBadgeColor(ask.venue)}`}>
                              {ask.venue}
                            </span>
                          ) : null}
                        </div>
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>

          {/* Overlap indicator (middle) */}
          <div className="bg-slate-900/40 border-b border-slate-800/30">
            <div className="flex flex-col items-center justify-center px-1 py-2">
              {overlapExists && overlapBid !== null && overlapAsk !== null ? (
                <div className="inline-flex items-center gap-2 rounded-full border border-amber-900/40 bg-amber-900/20 px-2 py-0.5">
                  <span className="text-[11px] font-semibold text-amber-300">
                    Overlap
                  </span>
                </div>
              ) : (
                <div className="text-[11px] font-semibold text-slate-400">
                  No overlap
                </div>
              )}

              <div className="mt-2 w-full px-1">
                {overlapExists && overlapBid !== null && overlapAsk !== null ? (
                  <>
                    <div className="grid grid-cols-2 gap-2">
                    <div className="rounded border border-green-900/40 bg-green-900/20 px-2 py-1">
                      <div className="text-[10px] font-semibold text-green-300">
                        Bid
                      </div>
                      <div className="mt-0.5 text-[12px] font-semibold text-green-300">
                        {formatDynamicPrice(overlapBid.price)}
                      </div>
                      <div className="text-[10px] text-slate-200">
                        {formatSize(overlapBid.size)}
                      </div>
                      {overlapBid.venue ? (
                        <div className="mt-1">
                          <span
                            className={`${badgeBase} ${venueBadgeColor(
                              overlapBid.venue
                            )}`}
                          >
                            {overlapBid.venue}
                          </span>
                        </div>
                      ) : null}
                    </div>

                    <div className="rounded border border-red-900/40 bg-red-900/20 px-2 py-1">
                      <div className="text-[10px] font-semibold text-red-300">
                        Ask
                      </div>
                      <div className="mt-0.5 text-[12px] font-semibold text-red-300">
                        {formatDynamicPrice(overlapAsk.price)}
                      </div>
                      <div className="text-[10px] text-slate-200">
                        {formatSize(overlapAsk.size)}
                      </div>
                      {overlapAsk.venue ? (
                        <div className="mt-1">
                          <span
                            className={`${badgeBase} ${venueBadgeColor(
                              overlapAsk.venue
                            )}`}
                          >
                            {overlapAsk.venue}
                          </span>
                        </div>
                      ) : null}
                    </div>
                    </div>
                    <div className="mt-2 text-[10px] leading-tight text-slate-400 text-center">
                      Overlap means the highest bid is at/above the lowest ask
                      ({formatDynamicPrice(overlapBid.price)} {" >="} {formatDynamicPrice(overlapAsk.price)}).
                      This indicates a crossed book and potential cross-venue arbitrage.
                    </div>
                  </>
                ) : (
                  <div className="text-center text-[10px] text-slate-500">
                    No overlap (Bid &lt; Ask)
                  </div>
                )}
              </div>
            </div>
          </div>

          {/* Bids panel (bottom) */}
          <div className="bg-slate-900/30">
            <div className="w-full">
              <div className="px-3 py-2 text-[11px] font-semibold text-slate-300">
                Bids ({base})
              </div>
              <div className="flex flex-col">
                {Array.from({ length: SIDE_ROWS }).map((_, i) => {
                  const bid = bidsBest[i];
                  const bidWidth =
                    bid && scale > 0
                      ? Math.min(100, (bid.size / scale) * 100)
                      : 0;

                  const isOverlapRow =
                    overlapExists && bid && bestAsk !== null && bid.price >= bestAsk;

                  return (
                    <div
                      key={`bid-${i}`}
                      className={[
                        "flex w-full items-stretch",
                        i % 2 === 0 ? "bg-slate-950/40" : "bg-slate-900/40",
                        isOverlapRow ? "bg-amber-900/20" : "",
                      ].join(" ")}
                      style={{ minHeight: 24 }}
                    >
                      <div className="w-1/2 flex items-center gap-2 px-3 py-1.5">
                        <div className="min-w-[62px]">
                          {bid?.venue ? (
                            <span className={`${badgeBase} ${venueBadgeColor(bid.venue)}`}>
                              {bid.venue}
                            </span>
                          ) : null}
                        </div>

                        <div className="flex-1">
                          {bid ? (
                            <div className="relative h-5 w-full bg-green-900/20 rounded-sm">
                              <div
                                className="absolute inset-y-0 left-0 bg-green-700/40 rounded-sm"
                                style={{ width: `${bidWidth}%` }}
                              />
                              <div className="relative z-10 flex h-full items-center justify-end pr-1 text-[11px] text-slate-200">
                                {formatSize(bid.size)}
                              </div>
                            </div>
                          ) : null}
                        </div>

                        <div className="w-24 text-right text-[11px] text-green-400 font-medium">
                          {bid ? formatDynamicPrice(bid.price) : ""}
                        </div>
                      </div>

                      <div className="flex-1" />
                    </div>
                  );
                })}
              </div>
            </div>
          </div>
        </div>
      </div>

      <div className="mt-2 flex justify-end text-[10px] text-slate-400">

        {lastUpdated ? `Last updated: ${new Date(lastUpdated)}` : ""}
      </div>
    </div>
  );
}
