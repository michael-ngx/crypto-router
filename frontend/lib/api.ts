/** Base for browser `fetch`. On Vercel, default to same-origin so `/api/*` hits the proxy route. */
export const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ??
  (process.env.VERCEL === "1" ? "" : "http://localhost:8080");
