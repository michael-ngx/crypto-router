/**
 * Same-origin `/api/...` → `app/api/[...path]/route.ts` → `BACKEND_URL` on the server.
 *
 * In **production** builds we ignore `NEXT_PUBLIC_API_BASE_URL` when it would make the
 * browser call `localhost` or plain `http://` (mixed content on HTTPS). Those values are
 * easy to leave in Vercel by mistake and cause `net::ERR_CONNECTION_REFUSED` to :8080.
 *
 * Set `NEXT_PUBLIC_API_BASE_URL` only for an **https** API on another host (rare here).
 */
function clientApiBase(): string {
  const raw = (process.env.NEXT_PUBLIC_API_BASE_URL ?? "")
    .trim()
    .replace(/\/+$/, "");
  if (process.env.NODE_ENV !== "production") {
    return raw;
  }
  if (!raw) return "";
  if (/^https?:\/\/(localhost|127\.0\.0\.1)(:\d+)?\/?$/i.test(raw)) {
    return "";
  }
  if (raw.startsWith("http://")) {
    return "";
  }
  return raw;
}

export const API_BASE_URL = clientApiBase();
