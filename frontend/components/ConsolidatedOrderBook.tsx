"use client";

import { useMemo } from "react";
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

// Multi-select venue checkboxes
function VenueSelector({
  availableVenues,
  selectedVenues,
  onSelectedVenuesChange,
}: {
  availableVenues: string[];
  selectedVenues: string[];
  onSelectedVenuesChange?: (venues: string[]) => void;
}) {
  if (availableVenues.length === 0 || !onSelectedVenuesChange) return null;

  const toggle = (venue: string) => {
    const next = selectedVenues.includes(venue)
      ? selectedVenues.filter((v) => v !== venue)
      : [...selectedVenues, venue].sort((a, b) =>
          a.toLowerCase().localeCompare(b.toLowerCase())
        );
    onSelectedVenuesChange(next);
  };

  const selectAll = () =>
    onSelectedVenuesChange(
      [...availableVenues].sort((a, b) =>
        a.toLowerCase().localeCompare(b.toLowerCase())
      )
    );
  const selectNone = () => onSelectedVenuesChange([]);

  return (
    <div className="flex flex-wrap items-center gap-3">
      <span className="text-[11px] font-medium text-slate-400">Exchanges</span>
      <div className="flex flex-wrap items-center gap-2">
        {availableVenues.map((venue) => {
          const checked = selectedVenues.includes(venue);
          return (
            <label
              key={venue}
              className="inline-flex cursor-pointer items-center gap-1.5 rounded-full border border-slate-700 bg-slate-950 px-2.5 py-1 text-[11px] text-slate-200 hover:bg-slate-800/50"
            >
              <input
                type="checkbox"
                checked={checked}
                onChange={() => toggle(venue)}
                className="h-3 w-3 rounded border-slate-600 text-blue-500 focus:ring-blue-500/50"
              />
              {venue}
            </label>
          );
        })}
        <button
          type="button"
          onClick={selectAll}
          className="text-[10px] text-slate-500 hover:text-slate-300"
        >
          All
        </button>
        <button
          type="button"
          onClick={selectNone}
          className="text-[10px] text-slate-500 hover:text-slate-300"
        >
          None
        </button>
      </div>
    </div>
  );
}

// Default venues shown when API hasn't returned yet
const DEFAULT_VENUES = ["Binance", "Coinbase", "Kraken", "OKX"];

/////////////////////////////////////////////////////////////////////////////
///////////////////////////////// Component /////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
type Props = {
  book: BookResponse | null;
  lastUpdated: number | null;
  statusMessage?: string | null;
  /** Venues available for selection (from API or default) */
  availableVenues?: string[];
  /** Currently selected venue names; empty = show all */
  selectedVenues?: string[];
  /** Called when user toggles venue selection */
  onSelectedVenuesChange?: (venues: string[]) => void;
};

export function ConsolidatedOrderBook({
  book,
  lastUpdated,
  statusMessage,
  availableVenues = DEFAULT_VENUES,
  selectedVenues = [],
  onSelectedVenuesChange,
}: Props) {
  const normalized = normalizeBook(book);

  // Determine filtered bids/asks based on selected venues
  // Empty selectedVenues = no venues selected = filter to empty (show "No order book data")
  const filtered = useMemo(() => {
    if (!normalized) return { bids: [] as OrderLevel[], asks: [] as OrderLevel[] };
    const { bids, asks } = normalized;
    if (selectedVenues.length === 0) return { bids: [], asks: [] };
    const active = new Set(selectedVenues);
    return {
      bids: bids.filter((b) => b.venue && active.has(b.venue)),
      asks: asks.filter((a) => a.venue && active.has(a.venue)),
    };
  }, [normalized, selectedVenues]);

  const hasFilteredData =
    filtered.bids.length > 0 || filtered.asks.length > 0;
  const hasAnyData =
    (normalized?.bids.length ?? 0) > 0 || (normalized?.asks.length ?? 0) > 0;
  const isLoading = !book;
  const hasSelection = selectedVenues.length > 0;

  // Loading / no data yet: show status message + venue selector
  if (!normalized || (!hasAnyData && isLoading)) {
    return (
      <div className="rounded-xl border border-slate-700 bg-slate-900/90 px-4 py-3">
        <VenueSelector
          availableVenues={availableVenues}
          selectedVenues={selectedVenues}
          onSelectedVenuesChange={onSelectedVenuesChange}
        />
        <div className="mt-3 text-sm text-slate-400">
          {statusMessage ?? "No book data yet."}
        </div>
      </div>
    );
  }

  // Always render full structure when we have normalized; empty rows when no data
  const { bids, asks } = filtered;
  const { base } = parseSymbol(normalized.symbol);

  const effectiveBids = bids;
  const effectiveAsks = asks;

  const SIDE_ROWS = 6;
  const SIDE_ROWS_WITH_OVERLAP = SIDE_ROWS + 1;
  const ROW_HEIGHT = 28; // Fixed row height so book doesn't shrink

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
  // Lowest (best) ask anchored at bottom row; pad top with empty slots so
  // higher asks stack upward as they come in.
  const asksReversed = asksBestLow.slice().reverse(); // [highest,...,lowest]
  const askPad = Math.max(0, SIDE_ROWS - asksReversed.length);
  const asksDisplay = [...Array(askPad).fill(null), ...asksReversed];

  const maxSizeAcross = Math.max(
    ...bidsBest.map((b) => b.size),
    ...asksBestLow.map((a) => a.size),
    0
  );
  const scale = computeScale(maxSizeAcross);

  const badgeBase = "inline-flex items-center rounded-full px-2 py-0.5 text-[10px] font-semibold";

  return (
    <div className="rounded-xl border border-slate-800 bg-slate-950 px-4 py-3">
      <div className="mb-3 flex flex-col gap-3">
        <div className="flex items-center justify-between">
          <div className="font-semibold uppercase tracking-wide text-slate-400">
            Order Book
          </div>
        </div>
        <VenueSelector
          availableVenues={availableVenues}
          selectedVenues={selectedVenues}
          onSelectedVenuesChange={onSelectedVenuesChange}
        />
      </div>

      <div className="overflow-hidden rounded-lg border border-slate-800 bg-slate-900/90 min-h-[340px]">
        <div className="flex flex-col">
          {/* Asks panel (top) */}
          <div className="border-b border-slate-800/30 bg-slate-900/30 min-h-[140px] flex-shrink-0">
            <div className="w-full">
              <div className="px-3 py-2 text-right text-[11px] font-semibold text-slate-300">
                Asks ({base})
              </div>
              <div className="flex w-full items-center">
                <div className="flex-1" />
                <div className="w-1/2 flex items-center gap-2 px-3 py-1 text-[10px] font-medium text-slate-500">
                  <div className="w-24 text-left">Price</div>
                  <div className="flex-1">Size</div>
                  <div className="min-w-[62px] text-right">Exchange</div>
                </div>
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
                        "flex w-full items-stretch flex-shrink-0",
                        i % 2 === 0 ? "bg-slate-950/40" : "bg-slate-900/40",
                        isOverlapRow ? "bg-amber-900/20" : "",
                      ].join(" ")}
                      style={{ height: ROW_HEIGHT }}
                    >
                      <div className="flex-1" />

                      <div className="w-1/2 flex items-center gap-2 px-3 py-1.5 flex-shrink-0">
                        <div className="w-24 text-left text-[11px] text-red-500 font-medium flex-shrink-0">
                          {ask ? formatDynamicPrice(ask.price) : "\u00A0"}
                        </div>

                        <div className="flex-1 min-w-0">
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
                          ) : (
                            <div className="h-5 w-full bg-red-900/10 rounded-sm" />
                          )}
                        </div>

                        <div className="min-w-[62px] text-right flex-shrink-0">
                          {ask?.venue ? (
                            <span className={`${badgeBase} ${venueBadgeColor(ask.venue)}`}>
                              {ask.venue}
                            </span>
                          ) : "\u00A0"}
                        </div>
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>

          {/* Overlap indicator (middle) */}
          <div className="bg-slate-900/40 border-b border-slate-800/30 flex-shrink-0">
            <div className="flex flex-col items-center justify-center px-1 py-2">
              {!hasFilteredData ? (
                <div className="text-center">
                  <div className="text-[11px] font-semibold text-slate-500">
                    {hasSelection && hasAnyData
                      ? "No order book data for the selected exchange(s)."
                      : !hasSelection && hasAnyData
                        ? "No exchanges selected. Select one or more to view the order book."
                        : statusMessage ?? "No order book data yet."}
                  </div>
                </div>
              ) : overlapExists && overlapBid !== null && overlapAsk !== null ? (
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
          <div className="bg-slate-900/30 min-h-[140px] flex-shrink-0">
            <div className="w-full">
              <div className="px-3 py-2 text-[11px] font-semibold text-slate-300">
                Bids ({base})
              </div>
              <div className="flex w-full items-center">
                <div className="w-1/2 flex items-center gap-2 px-3 py-1 text-[10px] font-medium text-slate-500">
                  <div className="min-w-[62px]">Exchange</div>
                  <div className="flex-1">Size</div>
                  <div className="w-24 text-right">Price</div>
                </div>
                <div className="flex-1" />
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
                        "flex w-full items-stretch flex-shrink-0",
                        i % 2 === 0 ? "bg-slate-950/40" : "bg-slate-900/40",
                        isOverlapRow ? "bg-amber-900/20" : "",
                      ].join(" ")}
                      style={{ height: ROW_HEIGHT }}
                    >
                      <div className="w-1/2 flex items-center gap-2 px-3 py-1.5 flex-shrink-0">
                        <div className="min-w-[62px] flex-shrink-0">
                          {bid?.venue ? (
                            <span className={`${badgeBase} ${venueBadgeColor(bid.venue)}`}>
                              {bid.venue}
                            </span>
                          ) : "\u00A0"}
                        </div>

                        <div className="flex-1 min-w-0">
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
                          ) : (
                            <div className="h-5 w-full bg-green-900/10 rounded-sm" />
                          )}
                        </div>

                        <div className="w-24 text-right text-[11px] text-green-400 font-medium flex-shrink-0">
                          {bid ? formatDynamicPrice(bid.price) : "\u00A0"}
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
