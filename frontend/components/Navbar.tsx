"use client";

import Link from "next/link";
import { usePathname, useRouter } from "next/navigation";
import { useEffect, useRef } from "react";
import { useAuth } from "../contexts/AuthContext";

export function Navbar() {
  const pathname = usePathname();
  const router = useRouter();
  const { isAuthenticated, user, logout } = useAuth();
  const shouldRedirectRef = useRef(false);

  // Handle logout and redirect in useEffect to avoid navigation during render
  useEffect(() => {
    if (shouldRedirectRef.current && !isAuthenticated) {
      shouldRedirectRef.current = false;
      router.push("/");
    }
  }, [isAuthenticated, router]);

  const handleLogout = () => {
    logout();
    shouldRedirectRef.current = true;
  };

  const linkClasses = (href: string) =>
    [
      "rounded-full px-3 py-1 text-sm border transition-colors",
      pathname === href
        ? "bg-blue-500/20 text-slate-50 border-blue-500/70"
        : "text-slate-300 border-transparent hover:text-slate-50 hover:border-slate-600",
    ].join(" ");

  return (
    <header className="border-b border-slate-800/80 bg-slate-950/80 backdrop-blur">
      <nav className="mx-auto flex max-w-5xl items-center justify-between px-6 py-3">
        <div className="text-xs font-semibold tracking-[0.25em] uppercase text-slate-300">
          Crypto Router
        </div>

        <div className="flex items-center gap-4">
          {isAuthenticated && (
            <div className="text-xs text-slate-400">
              {user?.first_name} {user?.last_name}
            </div>
          )}
          <div className="flex gap-2">
            {isAuthenticated ? (
              <>
                <Link href="/trade" className={linkClasses("/trade")}>Trade</Link>
                <Link href="/posttrade" className={linkClasses("/posttrade")}>Orders</Link>
              </>
            ) : (
              <Link href="/" className={linkClasses("/")}>Home</Link>
            )}
          </div>
          {isAuthenticated && (
            <button
              onClick={handleLogout}
              className="rounded-full px-3 py-1 text-sm border border-transparent text-slate-300 hover:text-slate-50 hover:border-slate-600 transition-colors"
            >
              Logout
            </button>
          )}
        </div>
      </nav>
    </header>
  );
}