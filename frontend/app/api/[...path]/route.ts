import { NextRequest, NextResponse } from 'next/server';

const BACKEND = 'http://18.117.8.179:8080/';

export async function GET(req: NextRequest, { params }: { params: { path: string[] } }) {
    const path = params.path.join('/');
    const search = req.nextUrl.search;
    const res = await fetch(${BACKEND}/${path}${search}, {
        headers: Object.fromEntries(req.headers),
    });
    const data = await res.text();
    return new NextResponse(data, { status: res.status,
        headers: { 'Content-Type': res.headers.get('Content-Type')  'application/json' }
    });
}

export async function POST(req: NextRequest, { params }: { params: { path: string[] } }) {
    const path = params.path.join('/');
    const body = await req.text();
    const res = await fetch(${BACKEND}/${path}, {
        method: 'POST',
        headers: Object.fromEntries(req.headers),
        body,
    });
    const data = await res.text();
    return new NextResponse(data, { status: res.status,
        headers: { 'Content-Type': res.headers.get('Content-Type')  'application/json' }
    });
}

export async function PATCH(req: NextRequest, { params }: { params: { path: string[] } }) {
    const path = params.path.join('/');
    const body = await req.text();
    const res = await fetch(${BACKEND}/${path}, {
        method: 'PATCH',
        headers: Object.fromEntries(req.headers),
        body,
    });
    const data = await res.text();
    return new NextResponse(data, { status: res.status,
        headers: { 'Content-Type': res.headers.get('Content-Type') || 'application/json' }
    });
}