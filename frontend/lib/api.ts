/**
 * Base for browser `fetch`. Prefer leaving `NEXT_PUBLIC_API_BASE_URL` unset on Vercel so
 * requests stay same-origin (`/api/...` → this deployment).
 *
 * Do not use `process.env.VERCEL` here: it is not available in the browser bundle, so the
 * client would fall back to localhost and every visitor would get ERR_CONNECTION_REFUSED.
 * `NODE_ENV === "production"` is inlined correctly for production builds (e.g. on Vercel).
 */
export const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ??
  (process.env.NODE_ENV === "production" ? "" : "http://localhost:8080");
