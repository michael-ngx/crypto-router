import type { ReactNode } from "react";
import "./globals.css";
import { Navbar } from "../components/Navbar";

export const metadata = {
  title: "Crypto Router",
  description: "Cryptocurrency Order Routing System",
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body className="min-h-screen bg-slate-950 text-slate-50 font-sans">
        <div className="flex min-h-screen flex-col">
          <Navbar />
          <main className="flex-1 w-full max-w-5xl mx-auto px-6 py-8">
            {children}
          </main>
        </div>
      </body>
    </html>
  );
}