#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_BIN="$ROOT_DIR/build/rpc_client.exe"
DURATION_FILE="$ROOT_DIR/build/stress_durations.txt"

# Defaults
CONCURRENT=${1:-10}
REQUESTS=${2:-100}

echo "=== RPC Stress Test ==="
echo "Concurrent clients: $CONCURRENT"
echo "Requests per client: $REQUESTS"
echo "Total requests:     $((CONCURRENT * REQUESTS))"
echo ""

if [ ! -f "$CLIENT_BIN" ]; then
    echo "ERROR: rpc_client.exe not found at $CLIENT_BIN"
    echo "Run 'bash rpc_build.sh' first."
    exit 1
fi

# Clear duration file
> "$DURATION_FILE"

START_MS=$(date +%s%3N)

# Launch concurrent clients
for i in $(seq 1 $CONCURRENT); do
    (
        for j in $(seq 1 $REQUESTS); do
            "$CLIENT_BIN" > /dev/null 2>&1
        done
    ) &
done

# Wait for all background jobs
echo "Running $CONCURRENT clients x $REQUESTS requests..."
wait

END_MS=$(date +%s%3N)
TOTAL_REQUESTS=$((CONCURRENT * REQUESTS))
ELAPSED_MS=$((END_MS - START_MS))
ELAPSED_S=$((ELAPSED_MS / 1000)).$((ELAPSED_MS % 1000))

if [ "$ELAPSED_MS" -gt 0 ]; then
    THROUGHPUT=$((TOTAL_REQUESTS * 1000 / ELAPSED_MS))
else
    THROUGHPUT="N/A"
fi

echo ""
echo "=== Results ==="
echo "Total time:    ${ELAPSED_S}s"
echo "Throughput:    ${THROUGHPUT} req/s"
echo "Concurrency:   $CONCURRENT"
echo "Req/client:    $REQUESTS"
echo ""
echo "NOTE: server/client are currently stubs (print-and-exit)."
echo "Real RPC stress testing requires server/client implementation."
