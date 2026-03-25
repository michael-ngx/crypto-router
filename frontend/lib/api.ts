/**
 * Same-origin `/api/...` is handled by `app/api/[...path]/route.ts`, which proxies to
 * `BACKEND_URL` (default `http://localhost:8080`). Use this in both dev and production so
 * the browser never calls `localhost:8080` directly (avoids ERR_CONNECTION_REFUSED when
 * the UI is open but the API is only reachable via the proxy).
 *
 * Set `NEXT_PUBLIC_API_BASE_URL` only when the browser must talk to a different origin
 * (e.g. debugging without the proxy). Do not use `process.env.VERCEL` here — it is not
 * available in the client bundle.
 */
export const API_BASE_URL = process.env.NEXT_PUBLIC_API_BASE_URL ?? "";
