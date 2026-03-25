/**
 * Base for browser `fetch`. Prefer leaving `NEXT_PUBLIC_API_BASE_URL` unset on Vercel so
 * requests stay same-origin (`/api/...` → this deployment). Setting it to the production
 * URL while you open a preview branch causes cross-origin calls and broken logins unless
 * the API route handles CORS (see `app/api/[...path]/route.ts`).
 */
export const API_BASE_URL =
  process.env.NEXT_PUBLIC_API_BASE_URL ??
  (process.env.VERCEL === "1" ? "" : "http://localhost:8080");
