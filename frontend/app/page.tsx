export default function HomePage() {
  return (
    <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
      <div className="w-full max-w-3xl rounded-2xl border border-slate-700/70 bg-slate-900/90 px-8 py-12 text-center shadow-[0_24px_80px_rgba(15,23,42,0.9)] bg-gradient-to-b from-blue-500/20 to-transparent">
        <h1 className="text-3xl md:text-4xl font-semibold tracking-[0.25em] uppercase text-slate-50 mb-3">
          Crypto Router
        </h1>
        <p className="mt-2 text-sm text-slate-400">
          Real-time routing, execution, and post-trade analytics.
        </p>
      </div>
    </div>
  );
}