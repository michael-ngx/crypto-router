import { NextRequest, NextResponse } from "next/server";

/** Server-side only: plain-HTTP backend URL (browser never sees this). */
function backendOrigin(): string {
  const raw = process.env.BACKEND_URL ?? "http://localhost:8080";
  return raw.replace(/\/$/, "");
}

function corsHeaders(req: NextRequest): Headers {
  const h = new Headers();
  const origin = req.headers.get("origin");
  if (origin) {
    h.set("Access-Control-Allow-Origin", origin);
    h.set("Vary", "Origin");
  } else {
    h.set("Access-Control-Allow-Origin", "*");
  }
  h.set("Access-Control-Allow-Methods", "GET, POST, PATCH, OPTIONS");
  h.set(
    "Access-Control-Allow-Headers",
    "Content-Type, Authorization, Accept, Cookie",
  );
  h.set("Access-Control-Max-Age", "86400");
  return h;
}

/** Browser calls /api/pairs → upstream http://backend/api/pairs */
function upstreamUrl(pathSegments: string[], search: string): string {
  const tail = pathSegments.length ? pathSegments.join("/") : "";
  const apiPath = tail ? `api/${tail}` : "api";
  return `${backendOrigin()}/${apiPath}${search}`;
}

function forwardHeaders(req: NextRequest): Headers {
  const out = new Headers();
  for (const name of ["authorization", "content-type", "accept", "cookie"]) {
    const v = req.headers.get(name);
    if (v) out.set(name, v);
  }
  return out;
}

async function proxy(
  req: NextRequest,
  pathSegments: string[],
  init: Omit<RequestInit, "headers">,
): Promise<NextResponse> {
  const url = upstreamUrl(pathSegments, req.nextUrl.search);
  try {
    const res = await fetch(url, {
      ...init,
      headers: forwardHeaders(req),
    });
    const data = await res.text();
    const contentType =
      res.headers.get("content-type") ?? "application/octet-stream";
    const headers = corsHeaders(req);
    headers.set("Content-Type", contentType);
    return new NextResponse(data, {
      status: res.status,
      headers,
    });
  } catch {
    return NextResponse.json(
      {
        error: "proxy_upstream_failed",
        message:
          "Could not reach the API server. On Vercel, set BACKEND_URL to http://YOUR_HOST:8080 (no NEXT_PUBLIC_).",
      },
      { status: 502, headers: corsHeaders(req) },
    );
  }
}

/** Browsers send this before cross-origin POST with JSON; without it Next returns 404. */
export async function OPTIONS(req: NextRequest) {
  return new NextResponse(null, { status: 204, headers: corsHeaders(req) });
}

export async function GET(
  req: NextRequest,
  ctx: { params: Promise<{ path: string[] }> },
) {
  const { path } = await ctx.params;
  return proxy(req, path, { method: "GET" });
}

export async function POST(
  req: NextRequest,
  ctx: { params: Promise<{ path: string[] }> },
) {
  const { path } = await ctx.params;
  const body = await req.text();
  return proxy(req, path, { method: "POST", body });
}

export async function PATCH(
  req: NextRequest,
  ctx: { params: Promise<{ path: string[] }> },
) {
  const { path } = await ctx.params;
  const body = await req.text();
  return proxy(req, path, { method: "PATCH", body });
}
