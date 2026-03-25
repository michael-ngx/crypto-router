import { NextRequest, NextResponse } from "next/server";

/** Server-side only: plain-HTTP backend URL (browser never sees this). */
function backendOrigin(): string {
  const raw = process.env.BACKEND_URL ?? "http://localhost:8080";
  return raw.replace(/\/$/, "");
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
  const res = await fetch(url, {
    ...init,
    headers: forwardHeaders(req),
  });
  const data = await res.text();
  const contentType =
    res.headers.get("content-type") ?? "application/octet-stream";
  return new NextResponse(data, {
    status: res.status,
    headers: { "Content-Type": contentType },
  });
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
