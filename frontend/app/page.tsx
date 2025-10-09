'use client';
import React, { useEffect, useRef, useState } from 'react';

type Tick = { symbol: string; bid: number; ask: number; ts_ms: number };

type Order = {
  id: string; symbol: string; side: 'BUY'|'SELL'; type: 'LIMIT'; price: number; qty: number; status: string; ts_ns: number
}

export default function Home() {
  const [tick, setTick] = useState<Tick | null>(null);
  const [orders, setOrders] = useState<Order[]>([]);
  const [form, setForm] = useState({ symbol: 'BTC-USD', side: 'BUY', price: 60000, qty: 0.01 });
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const ws = new WebSocket('ws://localhost:9001/ws');
    ws.onmessage = (ev) => {
      try { setTick(JSON.parse(ev.data)); } catch {}
    };
    wsRef.current = ws;
    return () => { ws.close(); };
  }, []);

  async function submitOrder(e: React.FormEvent) {
    e.preventDefault();
    const res = await fetch('http://localhost:9001/orders', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        symbol: form.symbol,
        side: form.side,
        type: 'LIMIT',
        price: Number(form.price),
        qty: Number(form.qty)
      })
    });
    const j = await res.json();
    if (j?.order) setOrders(prev => [j.order, ...prev]);
  }

  return (
    <main style={{ maxWidth: 860, margin: '40px auto', fontFamily: 'ui-sans-serif, system-ui' }}>
      <h1>Crypto Router MVP</h1>

      <section style={{ border: '1px solid #ddd', padding: 16, borderRadius: 12, marginTop: 16 }}>
        <h2>Live Quote</h2>
        {tick ? (
          <div>
            <div>Symbol: <b>{tick.symbol}</b></div>
            <div>Bid: <b>{tick.bid.toFixed(2)}</b> | Ask: <b>{tick.ask.toFixed(2)}</b></div>
            <div style={{ fontSize: 12, color: '#666' }}>ts: {new Date(tick.ts_ms).toLocaleTimeString()}</div>
          </div>
        ) : <div>Connecting…</div>}
      </section>

      <section style={{ border: '1px solid #ddd', padding: 16, borderRadius: 12, marginTop: 16 }}>
        <h2>Place Limit Order</h2>
        <form onSubmit={submitOrder} style={{ display: 'grid', gap: 8, gridTemplateColumns: '1fr 1fr 1fr 1fr auto' }}>
          <input value={form.symbol} onChange={e=>setForm({...form, symbol:e.target.value})} placeholder="Symbol"/>
          <select value={form.side} onChange={e=>setForm({...form, side:e.target.value})}>
            <option>BUY</option>
            <option>SELL</option>
          </select>
          <input type="number" step="0.01" value={form.price} onChange={e=>setForm({...form, price:Number(e.target.value)})} placeholder="Price"/>
          <input type="number" step="0.0001" value={form.qty} onChange={e=>setForm({...form, qty:Number(e.target.value)})} placeholder="Qty"/>
          <button type="submit">Submit</button>
        </form>
      </section>

      <section style={{ border: '1px solid #ddd', padding: 16, borderRadius: 12, marginTop: 16 }}>
        <h2>Orders (session)</h2>
        <table style={{ width: '100%', borderCollapse: 'collapse' }}>
          <thead>
            <tr>
              <th align="left">ID</th><th align="left">Symbol</th><th align="left">Side</th><th align="right">Price</th><th align="right">Qty</th><th align="left">Status</th>
            </tr>
          </thead>
          <tbody>
            {orders.map(o => (
              <tr key={o.id}>
                <td>{o.id}</td>
                <td>{o.symbol}</td>
                <td>{o.side}</td>
                <td align="right">{o.price}</td>
                <td align="right">{o.qty}</td>
                <td>{o.status}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>
    </main>
  );
}