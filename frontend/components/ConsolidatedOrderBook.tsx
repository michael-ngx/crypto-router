"use client";

export type OrderLevel = {
  price: number;
  size: number;
  venue: string;
};

export type BookResponse = {
  symbol: string;
  bids: OrderLevel[];
  asks: OrderLevel[];
  timestamp?: number | string;
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

// Format price display with 2 decimal places and thousands separator
function formatPrice(x: number): string {
  return x.toLocaleString(undefined, {
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  });
}

// Get badge color classes based on venue name
function venueBadgeColor(venue: string): string {

  const lookupDict: { [key: string]: string } = {
    kraken: "bg-purple-500 text-white",
    coinbase: "bg-blue-500 text-white",
    binance: "bg-yellow-400 text-slate-900",
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
};

export function ConsolidatedOrderBook({ book, lastUpdated }: Props) {
  const normalized = normalizeBook(book);

  if (!normalized || (normalized.bids.length === 0 && normalized.asks.length === 0)) {
    return (
      <div className="rounded-xl border border-slate-700 bg-slate-900/90 px-4 py-3 text-sm text-slate-400">
        No book data yet.
      </div>
    );
  }

  const { bids, asks } = normalized;
  const { base, quote } = parseSymbol(normalized.symbol);

  const maxSizeAcross = Math.max(
    ...bids.map((b) => b.size),
    ...asks.map((a) => a.size),
    0
  );
  const scale = computeScale(maxSizeAcross);
  const rows = Math.max(bids.length, asks.length);

  const badgeBase = "inline-flex items-center rounded-full px-2 py-0.5 text-[10px] font-semibold";

  return (
    <div className="rounded-xl border border-slate-800 bg-slate-950 px-4 py-3">
      <div className="overflow-hidden rounded-lg border border-slate-800 bg-white">
        <table className="min-w-full text-xs">
          <thead className="bg-slate-100">
            <tr className="text-left text-[11px] font-semibold text-slate-700 uppercase tracking-wide">
              <th className="px-3 py-2 w-24">Exchange</th>
              <th className="px-3 py-2 w-32">Size ({base})</th>
              <th className="px-3 py-2 w-32">Price ({quote})</th>

              <th className="px-3 py-2 w-32 text-right">Price ({quote})</th>
              <th className="px-3 py-2 w-32 text-right">Size ({base})</th>
              <th className="px-3 py-2 w-24 text-right">Exchange</th>
            </tr>
          </thead>
          <tbody>
            {Array.from({ length: rows }).map((_, i) => {
              const bid = bids[i];
              const ask = asks[i];

              const bidWidth =
                bid && scale > 0 ? Math.min(100, (bid.size / scale) * 100) : 0;
              const askWidth =
                ask && scale > 0 ? Math.min(100, (ask.size / scale) * 100) : 0;

              return (
                <tr
                  key={i}
                  className={i % 2 === 0 ? "bg-white" : "bg-slate-50"}
                >
                  {/* Bids left */}
                  <td className="px-3 py-1.5 align-middle">
                    {bid?.venue ? (
                      <span
                        className={`${badgeBase} ${venueBadgeColor(
                          bid.venue
                        )}`}
                      >
                        {bid.venue}
                      </span>
                    ) : null}
                  </td>
                  <td className="px-3 py-1.5 align-middle">
                    {bid ? (
                      <div className="relative h-5 w-full bg-green-50">
                        <div
                          className="absolute inset-y-0 left-0 bg-green-200"
                          style={{ width: `${bidWidth}%` }}
                        />
                        <div className="relative z-10 flex h-full items-center justify-end pr-1 text-[11px] text-slate-800">
                          {formatSize(bid.size)}
                        </div>
                      </div>
                    ) : null}
                  </td>
                  <td className="px-3 py-1.5 align-middle text-[11px] text-slate-800">
                    {bid ? formatPrice(bid.price) : ""}
                  </td>

                  {/* Asks right */}
                  <td className="px-3 py-1.5 align-middle text-right text-[11px] text-red-500">
                    {ask ? formatPrice(ask.price) : ""}
                  </td>
                  <td className="px-3 py-1.5 align-middle">
                    {ask ? (
                      <div className="relative h-5 w-full bg-red-50">
                        <div
                          className="absolute inset-y-0 right-0 bg-red-200"
                          style={{ width: `${askWidth}%` }}
                        />
                        <div className="relative z-10 flex h-full items-center justify-start pl-1 text-[11px] text-slate-800">
                          {formatSize(ask.size)}
                        </div>
                      </div>
                    ) : null}
                  </td>
                  <td className="px-3 py-1.5 align-middle text-right">
                    {ask?.venue ? (
                      <span
                        className={`${badgeBase} ${venueBadgeColor(
                          ask.venue
                        )}`}
                      >
                        {ask.venue}
                      </span>
                    ) : null}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      <div className="mt-2 flex justify-between text-[10px] text-slate-400">
        <div>
          <span className="text-red-500 font-medium">Red</span> prices indicate ask side. Crossed levels logic can be added later.
        </div>

        <div className="text-right">
          {lastUpdated ? `Last updated: ${new Date(lastUpdated)}` : ""}
        </div>
      </div>
    </div>
  );
}