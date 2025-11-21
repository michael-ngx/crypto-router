"use client";

import { useState, useEffect } from "react";
import { useAuth } from "../contexts/AuthContext";
import { useRouter } from "next/navigation";

export default function HomePage() {
  const { isLoading: authLoading, isAuthenticated, login, signup } = useAuth();
  const router = useRouter();
  const [showAuthModal, setShowAuthModal] = useState(false);
  const [isLoginMode, setIsLoginMode] = useState(true);
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [firstName, setFirstName] = useState("");
  const [lastName, setLastName] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [shouldRedirect, setShouldRedirect] = useState(false);

  // Redirect if already authenticated or after successful login/signup
  useEffect(() => {
    if (!authLoading && (isAuthenticated || shouldRedirect)) {
      router.push("/trade");
    }
  }, [authLoading, isAuthenticated, shouldRedirect, router]);

  // Reset redirect state when auth state changes (e.g., on logout)
  useEffect(() => {
    if (!isAuthenticated) {
      setShouldRedirect(false);
      // Close modal if open when logging out
      setShowAuthModal(false);
      setError(null);
    }
  }, [isAuthenticated]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);
    setIsSubmitting(true);

    try {
      if (isLoginMode) {
        await login(email, password);
        setShouldRedirect(true);
      } else {
        if (password.length < 6) {
          throw new Error("Password must be at least 6 characters");
        }
        await signup(email, password, firstName, lastName);
        setShouldRedirect(true);
      }
    } catch (err: any) {
      setError(err?.message || (isLoginMode ? "Login failed" : "Signup failed"));
      setIsSubmitting(false);
    }
  };

  // Show loading state while checking auth or if authenticated (will redirect)
  // This ensures consistent hook execution
  if (authLoading) {
    return (
      <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
        <div className="text-slate-400">Loading...</div>
      </div>
    );
  }

  // If authenticated, show loading while redirect happens
  if (isAuthenticated) {
    return (
      <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
        <div className="text-slate-400">Loading...</div>
      </div>
    );
  }

  return (
    <div className="flex min-h-[calc(100vh-4rem)] items-center justify-center">
      <div className="w-full max-w-3xl rounded-2xl border border-slate-700/70 bg-slate-900/90 px-8 py-12 text-center shadow-[0_24px_80px_rgba(15,23,42,0.9)] bg-gradient-to-b from-blue-500/20 to-transparent">
        <h1 className="text-3xl md:text-4xl font-semibold tracking-[0.25em] uppercase text-slate-50 mb-3">
          Crypto Router
        </h1>
        <p className="mt-2 text-sm text-slate-400 mb-8">
          Real-time routing, execution, and post-trade analytics.
        </p>

        <div className="flex justify-center">
          <button
            onClick={() => {
              setIsLoginMode(true);
              setShowAuthModal(true);
              setError(null);
            }}
            className="rounded-full border border-slate-700 bg-slate-950 px-6 py-2 text-sm font-medium text-slate-100 transition-colors hover:bg-slate-800 hover:border-slate-600 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
          >
            Login
          </button>
        </div>
      </div>

      {/* Auth Modal */}
      {showAuthModal && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50 p-4">
          <div className="w-full max-w-md rounded-lg border border-slate-700 bg-slate-900/95 backdrop-blur-sm p-8 relative shadow-[0_24px_80px_rgba(15,23,42,0.9)]">
            <button
              onClick={() => {
                setShowAuthModal(false);
                setError(null);
                setEmail("");
                setPassword("");
                setFirstName("");
                setLastName("");
              }}
              className="absolute top-4 right-4 text-slate-400 hover:text-slate-200 transition-colors"
            >
              ✕
            </button>

            <h1 className="mb-6 text-xl font-semibold text-slate-50">
              {isLoginMode ? "Login" : "Sign Up"}
            </h1>

            {error && (
              <div className="mb-4 rounded-lg border border-red-500/40 bg-red-950/40 px-3 py-2 text-sm text-red-200">
                {error}
              </div>
            )}

            <form onSubmit={handleSubmit} className="flex flex-col gap-4">
              {!isLoginMode && (
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <label className="mb-1 block text-sm font-medium text-slate-200">
                      First Name
                    </label>
                    <input
                      type="text"
                      value={firstName}
                      onChange={(e) => setFirstName(e.target.value)}
                      required={!isLoginMode}
                      className="w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
                      placeholder="John"
                    />
                  </div>

                  <div>
                    <label className="mb-1 block text-sm font-medium text-slate-200">
                      Last Name
                    </label>
                    <input
                      type="text"
                      value={lastName}
                      onChange={(e) => setLastName(e.target.value)}
                      required={!isLoginMode}
                      className="w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
                      placeholder="Doe"
                    />
                  </div>
                </div>
              )}

              <div>
                <label className="mb-1 block text-sm font-medium text-slate-200">
                  Email
                </label>
                <input
                  type="email"
                  value={email}
                  onChange={(e) => setEmail(e.target.value)}
                  required
                  className="w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
                  placeholder="you@example.com"
                />
              </div>

              <div>
                <label className="mb-1 block text-sm font-medium text-slate-200">
                  Password
                </label>
                <input
                  type="password"
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  required
                  minLength={6}
                  className="w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 focus:outline-none focus:ring-2 focus:ring-blue-500/70"
                  placeholder="••••••••"
                />
                {!isLoginMode && (
                  <p className="mt-1 text-xs text-slate-400">
                    Must be at least 6 characters
                  </p>
                )}
              </div>

              <button
                type="submit"
                disabled={isSubmitting}
                className="rounded-full border border-slate-700 bg-slate-950 px-4 py-2 text-sm font-medium text-slate-100 transition-colors hover:bg-slate-800 hover:border-slate-600 focus:outline-none focus:ring-2 focus:ring-blue-500/70 disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:bg-slate-950 disabled:hover:border-slate-700"
              >
                {isSubmitting
                  ? isLoginMode
                    ? "Logging in..."
                    : "Creating account..."
                  : isLoginMode
                  ? "Login"
                  : "Sign Up"}
              </button>
            </form>

            <p className="mt-4 text-center text-xs text-slate-400">
              {isLoginMode ? "Don't have an account? " : "Already have an account? "}
              <button
                onClick={() => {
                  setIsLoginMode(!isLoginMode);
                  setError(null);
                }}
                className="text-blue-400 hover:text-blue-300 underline transition-colors"
              >
                {isLoginMode ? "Sign Up" : "Login"}
              </button>
            </p>
          </div>
        </div>
      )}
    </div>
  );
}