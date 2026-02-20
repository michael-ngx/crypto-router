"use client";

import Link from "next/link";
import { useEffect, useMemo, useState, type ReactElement } from "react";
import { useParams, useRouter } from "next/navigation";
import { useAuth } from "../../../../contexts/AuthContext";

const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ?? "http://localhost:8080";

type StageTone = "complete" | "current" | "upcoming" | "failed";

interface OrderDetails {
  id: string;
  user_id: string;
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
  failure_code: string | null;
  failure_message: string | null;
  created_at: string;
  execution_started_at: string | null;
  terminal_at: string | null;
  last_updated_at: string | null;
}

interface OrderLeg {
  id: string;
  venue: string;
  status: string;
  quantity_planned: number;
  limit_price: number | null;
  price_planned: number;
  quantity_submitted: number | null;
  price_submitted: number | null;
  quantity_filled: number;
  price_filled_avg: number | null;
  client_order_id: string | null;
  venue_order_id: string | null;
  error_code: string | null;
  error_message: string | null;
  created_at: string;
  submitted_at: string | null;
  acknowledged_at: string | null;
  first_fill_at: string | null;
  last_fill_at: string | null;
  terminal_at: string | null;
  last_updated_at: string | null;
}

interface OrderDetailsResponse {
  order: OrderDetails;
  legs: OrderLeg[];
}

function formatTs(ts: string | null): string {
  if (!ts) return "N/A";
  return new Date(ts).toLocaleString();
}

function formatQty(v: number | null): string {
  if (v == null) return "N/A";
  return v.toLocaleString(undefined, { maximumFractionDigits: 8 });
}

function formatPx(v: number | null): string {
  if (v == null) return "N/A";
  return v.toLocaleString(undefined, { maximumFractionDigits: 2 });
}

function toneClasses(tone: StageTone): string {
  switch (tone) {
    case "complete":
      return "border-green-500/40 bg-green-950/20";
    case "current":
      return "border-yellow-500/40 bg-yellow-950/20";
    case "upcoming":
      return "border-orange-500/40 bg-orange-950/20";
    case "failed":
      return "border-red-500/60 bg-red-950/30";
    default:
      return "border-slate-700 bg-slate-900/50";
  }
}

function toneLabel(tone: StageTone): string {
  switch (tone) {
    case "complete":
      return "Completed";
    case "current":
      return "Current";
    case "upcoming":
      return "Pending";
    case "failed":
      return "Failed";
    default:
      return "Unknown";
  }
}

function toneTextClass(tone: StageTone): string {
  switch (tone) {
    case "complete":
      return "text-green-300";
    case "current":
      return "text-yellow-300";
    case "upcoming":
      return "text-orange-300";
    case "failed":
      return "text-red-300";
    default:
      return "text-slate-300";
  }
}

function isTerminalStatus(status: string): boolean {
  return (
    status === "filled" ||
    status === "failed" ||
    status === "cancelled" ||
    status === "expired"
  );
}

function isFailureTerminal(status: string): boolean {
  return status === "failed" || status === "cancelled" || status === "expired";
}

export default function OrderDetailsPage() {
  const { isAuthenticated, isLoading: authLoading, user } = useAuth();
  const router = useRouter();
  const params = useParams<{ order_id: string }>();
  const orderIdParam = params?.order_id;
  const orderId = Array.isArray(orderIdParam) ? orderIdParam[0] : orderIdParam;

  const [order, setOrder] = useState<OrderDetails | null>(null);
  const [legs, setLegs] = useState<OrderLeg[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [isRefreshing, setIsRefreshing] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!authLoading && !isAuthenticated) {
      router.push("/");
    }
  }, [authLoading, isAuthenticated, router]);

  useEffect(() => {
    if (authLoading || !isAuthenticated || !user || !orderId) return;

    let cancelled = false;
    const fetchDetails = async (backgroundRefresh: boolean) => {
      if (backgroundRefresh) {
        setIsRefreshing(true);
      } else {
        setIsLoading(true);
      }
      setError(null);
      try {
        const response = await fetch(
          `${API_BASE_URL}/api/orders/${encodeURIComponent(
            orderId
          )}/details?user_id=${encodeURIComponent(user.user_id)}`
        );
        if (!response.ok) {
          const err = await response.json().catch(() => ({}));
          throw new Error(err.error || `HTTP ${response.status}`);
        }
        const data: OrderDetailsResponse = await response.json();
        if (cancelled) return;
        setOrder(data.order);
        setLegs(data.legs || []);
      } catch (err: unknown) {
        if (cancelled) return;
        const msg =
          err instanceof Error ? err.message : "Failed to fetch order details";
        setError(msg);
      } finally {
        if (!cancelled) {
          if (backgroundRefresh) {
            setIsRefreshing(false);
          } else {
            setIsLoading(false);
          }
        }
      }
    };

    void fetchDetails(false);
    const interval = setInterval(() => {
      void fetchDetails(true);
    }, 5000);
    return () => {
      cancelled = true;
      clearInterval(interval);
    };
  }, [authLoading, isAuthenticated, user, orderId]);

  const derived = useMemo(() => {
    if (!order) return null;

    const hasPlan = legs.length > 0 && order.quantity_planned > 0;
    const hasSubmitted = legs.some(
      (l) => l.submitted_at != null || l.quantity_submitted != null || l.status !== "planned"
    );
    const hasAcknowledged = legs.some(
      (l) => l.acknowledged_at != null || l.venue_order_id != null
    );
    const hasFills = legs.some(
      (l) => l.first_fill_at != null || l.last_fill_at != null || l.quantity_filled > 0
    );
    const isTerminal = isTerminalStatus(order.status) || order.terminal_at != null;
    const isFailure = isFailureTerminal(order.status);

    // 0=request, 1=plan, 2=submission, 3=ack, 4=fills, 5=terminal
    const reached = [true, hasPlan, hasSubmitted, hasAcknowledged, hasFills, isTerminal];

    let failureStage = -1;
    if (isFailure) {
      if (!hasSubmitted) failureStage = 2;
      else if (!hasAcknowledged) failureStage = 3;
      else if (!hasFills) failureStage = 4;
      else failureStage = 5;
    }

    let currentStage = 5;
    if (!isTerminal) {
      currentStage = reached.findIndex((r) => !r);
      if (currentStage < 0) currentStage = 5;
    }

    return {
      hasPlan,
      hasSubmitted,
      hasAcknowledged,
      hasFills,
      isTerminal,
      isFailure,
      reached,
      failureStage,
      currentStage,
    };
  }, [order, legs]);

  if (authLoading || !isAuthenticated) {
    return (
      <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
        <div className="text-slate-400">Loading...</div>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-xl font-semibold text-slate-50">Order Details</h2>
          <p className="text-xs text-slate-400">
            Full timeline for order{" "}
            <span className="font-mono text-slate-300">{orderId ?? "unknown"}</span>
          </p>
          <p
            className={`text-[11px] text-slate-500 ${
              isRefreshing && !isLoading ? "visible" : "invisible"
            }`}
            aria-hidden={!isRefreshing || isLoading}
          >
            Updating...
          </p>
        </div>
        <Link
          href="/posttrade"
          className="rounded-full border border-slate-700 bg-slate-900 px-3 py-1.5 text-xs text-slate-200 transition-colors hover:border-slate-600 hover:text-slate-50"
        >
          Back to Post-Trade
        </Link>
      </div>

      {error && (
        <div className="rounded-lg border border-red-500/50 bg-red-950/40 px-4 py-3 text-sm text-red-200">
          ! {error}
        </div>
      )}

      {isLoading || !order || !derived ? (
        <div className="rounded-lg border border-slate-700 bg-slate-900/50 p-6 text-slate-400">
          Loading order timeline...
        </div>
      ) : (
        <div className="flex flex-col gap-4">
          {(() => {
            const blocks: Array<{
              key: string;
              title: string;
              tone: StageTone;
              body: ReactElement;
            }> = [];

            const addBlock = (
              key: string,
              title: string,
              tone: StageTone,
              body: ReactElement
            ) => {
              blocks.push({ key, title, tone, body });
            };

            const failureMessage =
              order.failure_message ||
              (order.status === "cancelled"
                ? "Order was cancelled before reaching the next stage."
                : order.status === "failed"
                ? "Order terminated due to execution failure."
                : order.status === "expired"
                ? "Order expired before completion."
                : "Order terminated before completion.");

            const stageTone = (idx: number): StageTone => {
              if (derived.isFailure && idx === derived.failureStage) return "failed";
              if (derived.reached[idx]) return "complete";
              if (idx === derived.currentStage) return "current";
              return "upcoming";
            };

            // Stage 0 - Request Submitted
            addBlock(
              "request",
              "1) Order Request Submitted",
              stageTone(0),
              <div className="grid gap-1 text-xs text-slate-300">
                <p>Created At: {formatTs(order.created_at)}</p>
                <p>
                  {order.side.toUpperCase()} {order.order_type.toUpperCase()} {order.symbol}
                </p>
                <p>Quantity Requested: {formatQty(order.quantity_requested)}</p>
                <p>Limit Price: {formatPx(order.limit_price)}</p>
              </div>
            );

            // Stage 1 - Router Planned
            addBlock(
              "planned",
              "2) Router Planned",
              stageTone(1),
              <div className="flex flex-col gap-3">
                <div className="grid gap-1 text-xs text-slate-300">
                  <p>Quantity Planned: {formatQty(order.quantity_planned)}</p>
                  <p>Planned Price Avg: {formatPx(order.price_planned_avg)}</p>
                  <p>Fully Routable: {order.fully_routable ? "Yes" : "No"}</p>
                  <p>Router Message: {order.routing_message ?? "N/A"}</p>
                </div>
                {legs.length > 0 ? (
                  <div className="overflow-x-auto rounded-lg border border-slate-700">
                    <table className="w-full text-xs">
                      <thead className="bg-slate-900/70 text-slate-300">
                        <tr>
                          <th className="px-3 py-2 text-left">Venue</th>
                          <th className="px-3 py-2 text-left">Qty Planned</th>
                          <th className="px-3 py-2 text-left">Limit Price</th>
                          <th className="px-3 py-2 text-left">Price Planned</th>
                          <th className="px-3 py-2 text-left">Status</th>
                        </tr>
                      </thead>
                      <tbody className="divide-y divide-slate-800 text-slate-200">
                        {legs.map((leg) => (
                          <tr key={leg.id}>
                            <td className="px-3 py-2">{leg.venue}</td>
                            <td className="px-3 py-2">{formatQty(leg.quantity_planned)}</td>
                            <td className="px-3 py-2">{formatPx(leg.limit_price)}</td>
                            <td className="px-3 py-2">{formatPx(leg.price_planned)}</td>
                            <td className="px-3 py-2">{leg.status}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                ) : (
                  <p className="text-xs text-slate-400">
                    No planned legs are recorded yet.
                  </p>
                )}
              </div>
            );

            const stopAtFailure = (idx: number) =>
              derived.isFailure && derived.failureStage === idx;

            // Stage 2 - Venue Submission
            addBlock(
              "submitted",
              "3) Venue Submission",
              stageTone(2),
              stopAtFailure(2) ? (
                <div className="rounded-md border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
                  ! {failureMessage}
                </div>
              ) : derived.hasSubmitted ? (
                <div className="overflow-x-auto rounded-lg border border-slate-700">
                  <table className="w-full text-xs">
                    <thead className="bg-slate-900/70 text-slate-300">
                      <tr>
                        <th className="px-3 py-2 text-left">Venue</th>
                        <th className="px-3 py-2 text-left">Qty Submitted</th>
                        <th className="px-3 py-2 text-left">Price Submitted</th>
                        <th className="px-3 py-2 text-left">Submitted At</th>
                        <th className="px-3 py-2 text-left">Status</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-800 text-slate-200">
                      {legs.map((leg) => (
                        <tr key={leg.id}>
                          <td className="px-3 py-2">{leg.venue}</td>
                          <td className="px-3 py-2">{formatQty(leg.quantity_submitted)}</td>
                          <td className="px-3 py-2">{formatPx(leg.price_submitted)}</td>
                          <td className="px-3 py-2">{formatTs(leg.submitted_at)}</td>
                          <td className="px-3 py-2">{leg.status}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              ) : (
                <p className="text-xs text-slate-400">
                  Not reached yet. Exchange execution worker has not submitted child orders.
                </p>
              )
            );

            if (!(derived.isFailure && derived.failureStage <= 2)) {
              // Stage 3 - Venue Acknowledgement
              addBlock(
                "ack",
                "4) Venue Acknowledgement",
                stageTone(3),
                stopAtFailure(3) ? (
                  <div className="rounded-md border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
                    ! {failureMessage}
                  </div>
                ) : derived.hasAcknowledged ? (
                  <div className="overflow-x-auto rounded-lg border border-slate-700">
                    <table className="w-full text-xs">
                      <thead className="bg-slate-900/70 text-slate-300">
                        <tr>
                          <th className="px-3 py-2 text-left">Venue</th>
                          <th className="px-3 py-2 text-left">Client Order ID</th>
                          <th className="px-3 py-2 text-left">Venue Order ID</th>
                          <th className="px-3 py-2 text-left">Acknowledged At</th>
                          <th className="px-3 py-2 text-left">Status</th>
                        </tr>
                      </thead>
                      <tbody className="divide-y divide-slate-800 text-slate-200">
                        {legs.map((leg) => (
                          <tr key={leg.id}>
                            <td className="px-3 py-2">{leg.venue}</td>
                            <td className="px-3 py-2">{leg.client_order_id ?? "N/A"}</td>
                            <td className="px-3 py-2">{leg.venue_order_id ?? "N/A"}</td>
                            <td className="px-3 py-2">{formatTs(leg.acknowledged_at)}</td>
                            <td className="px-3 py-2">{leg.status}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                ) : (
                  <p className="text-xs text-slate-400">
                    Not reached yet. No exchange acknowledgement has been recorded.
                  </p>
                )
              );
            }

            if (!(derived.isFailure && derived.failureStage <= 3)) {
              // Stage 4 - Fill Progress
              addBlock(
                "fills",
                "5) Fill Progress",
                stageTone(4),
                stopAtFailure(4) ? (
                  <div className="rounded-md border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
                    ! {failureMessage}
                  </div>
                ) : derived.hasFills ? (
                  <div className="overflow-x-auto rounded-lg border border-slate-700">
                    <table className="w-full text-xs">
                      <thead className="bg-slate-900/70 text-slate-300">
                        <tr>
                          <th className="px-3 py-2 text-left">Venue</th>
                          <th className="px-3 py-2 text-left">Qty Filled</th>
                          <th className="px-3 py-2 text-left">Avg Fill Price</th>
                          <th className="px-3 py-2 text-left">First Fill At</th>
                          <th className="px-3 py-2 text-left">Last Fill At</th>
                          <th className="px-3 py-2 text-left">Status</th>
                        </tr>
                      </thead>
                      <tbody className="divide-y divide-slate-800 text-slate-200">
                        {legs.map((leg) => (
                          <tr key={leg.id}>
                            <td className="px-3 py-2">{leg.venue}</td>
                            <td className="px-3 py-2">{formatQty(leg.quantity_filled)}</td>
                            <td className="px-3 py-2">{formatPx(leg.price_filled_avg)}</td>
                            <td className="px-3 py-2">{formatTs(leg.first_fill_at)}</td>
                            <td className="px-3 py-2">{formatTs(leg.last_fill_at)}</td>
                            <td className="px-3 py-2">{leg.status}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                ) : (
                  <p className="text-xs text-slate-400">
                    Not reached yet. No fills have been recorded from venues.
                  </p>
                )
              );
            }

            if (!(derived.isFailure && derived.failureStage <= 4)) {
              // Stage 5 - Terminal
              addBlock(
                "terminal",
                "6) Terminal Outcome",
                stageTone(5),
                derived.isFailure ? (
                  <div className="flex flex-col gap-2">
                    <div className="rounded-md border border-red-500/40 bg-red-950/40 px-3 py-2 text-xs text-red-200">
                      ! {failureMessage}
                    </div>
                    <p className="text-xs text-slate-300">Status: {order.status}</p>
                    <p className="text-xs text-slate-300">
                      Terminal At: {formatTs(order.terminal_at)}
                    </p>
                    <p className="text-xs text-slate-300">
                      Failure Code: {order.failure_code ?? "N/A"}
                    </p>
                  </div>
                ) : derived.isTerminal ? (
                  <div className="grid gap-1 text-xs text-slate-300">
                    <p>Status: {order.status}</p>
                    <p>Terminal At: {formatTs(order.terminal_at)}</p>
                    <p>Quantity Filled (Total): {formatQty(order.quantity_filled)}</p>
                    <p>Average Fill Price: {formatPx(order.price_filled_avg)}</p>
                  </div>
                ) : (
                  <p className="text-xs text-slate-400">
                    Not reached yet. Order is still active and has not entered a terminal state.
                  </p>
                )
              );
            }

            return blocks.map((block) => (
              <section
                key={block.key}
                className={`rounded-lg border px-4 py-4 ${toneClasses(block.tone)}`}
              >
                <div className="mb-3 flex items-center justify-between">
                  <h3 className="text-sm font-semibold text-slate-100">{block.title}</h3>
                  <span
                    className={`rounded-full border px-2 py-0.5 text-[11px] ${toneTextClass(
                      block.tone
                    )} border-current/40`}
                  >
                    {toneLabel(block.tone)}
                  </span>
                </div>
                {block.body}
              </section>
            ));
          })()}
        </div>
      )}
    </div>
  );
}
