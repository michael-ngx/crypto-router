#!/bin/bash
# Quick diagnostic for /api/book - run with backend server on localhost:8080
# Usage: ./scripts/test_book_api.sh [symbol]
SYMBOL="${1:-BTC-USD}"
echo "Testing /api/book?symbol=$SYMBOL&depth=10"
echo "---"
curl -s "http://localhost:8080/api/book?symbol=$SYMBOL&depth=10" | python3 -c "
import json, sys
try:
    d = json.load(sys.stdin)
    status = d.get('status', {})
    print('Status:', status.get('message', 'N/A'))
    print('Code:', status.get('code', 'N/A'))
    print('Venues:', d.get('venues', []))
    print('Bids:', len(d.get('bids', [])))
    print('Asks:', len(d.get('asks', [])))
    if d.get('bids'):
        print('Top bid:', d['bids'][0])
    if d.get('asks'):
        print('Top ask:', d['asks'][0])
except Exception as e:
    print('Error:', e)
    print(sys.stdin.read())
"
