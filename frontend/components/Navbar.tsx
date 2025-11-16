"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

export function Navbar() {
  const pathname = usePathname();

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

        <div className="flex gap-2">
          <Link href="/" className={linkClasses("/")}>Home</Link>
          <Link href="/trade" className={linkClasses("/trade")}>Trade</Link>
          <Link href="/posttrade" className={linkClasses("/posttrade")}>Post-trade</Link>
        </div>
      </nav>
    </header>
  );
}