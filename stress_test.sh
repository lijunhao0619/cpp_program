#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_BIN="$ROOT_DIR/build/rpc_client.exe"
export PATH="/e/msys/ucrt64/bin:$PATH"
RESULTS_DIR="$ROOT_DIR/build/stress_results"

CONCURRENT=${1:-10}
REQUESTS=${2:-100}

echo "=== RPC Stress Test ==="
echo "Concurrent clients: $CONCURRENT"
echo "Requests per client: $REQUESTS"
echo "Total requests:      $((CONCURRENT * REQUESTS))"
echo ""

if [ ! -f "$CLIENT_BIN" ]; then
    echo "ERROR: rpc_client.exe not found at $CLIENT_BIN"
    exit 1
fi

# 检查服务端是否在运行（跨平台：netstat + grep）
SERVER_HOST="${2:-127.0.0.1}"
SERVER_PORT="${3:-8080}"
if ! netstat -ano 2>/dev/null | grep -q ":$SERVER_PORT.*LISTENING"; then
    echo "WARNING: port $SERVER_PORT may not be listening"
    echo "Make sure server is running: ./build/rpc_server.exe"
fi

rm -rf "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR"

echo "Launching $CONCURRENT concurrent clients..."
START_EPOCH=$(date +%s%3N)

# ── 启动并发客户端 ──
for i in $(seq 1 $CONCURRENT); do
    (
        "$CLIENT_BIN" --stress "$REQUESTS" > "$RESULTS_DIR/result_$i.txt" 2>&1
    ) &
done

wait
END_EPOCH=$(date +%s%3N)
WALL_MS=$((END_EPOCH - START_EPOCH))

# ── 聚合结果 ──
total_success=0
total_fail=0
total_qps=0
sum_avg=0
sum_min=0
sum_max=0
clients_ok=0
clients_fail=0

# awk helper for key=value parsing
parse_val() {
    echo "$1" | awk -v key="$2" '{
        for (i=1; i<=NF; i++) {
            split($i, kv, "=")
            if (kv[1] == key) print kv[2]
        }
    }'
}

for f in "$RESULTS_DIR"/result_*.txt; do
    line=$(cat "$f")
    if echo "$line" | grep -q "STRESS OK"; then
        clients_ok=$((clients_ok + 1))

        success=$(parse_val "$line" "success")
        fail=$(parse_val "$line" "fail")
        qps=$(parse_val "$line" "qps")
        avg=$(parse_val "$line" "avg_ms")
        min=$(parse_val "$line" "min_ms")
        max=$(parse_val "$line" "max_ms")

        total_success=$((total_success + success))
        total_fail=$((total_fail + fail))
        sum_avg=$(awk "BEGIN { printf \"%.3f\", $sum_avg + $avg }")

        # track overall min/max across clients
        if [ "$clients_ok" -eq 1 ]; then
            overall_min=$min
            overall_max=$max
        else
            overall_min=$(awk "BEGIN { if ($min < $overall_min) printf \"%.3f\", $min; else printf \"%.3f\", $overall_min }")
            overall_max=$(awk "BEGIN { if ($max > $overall_max) printf \"%.3f\", $max; else printf \"%.3f\", $overall_max }")
        fi
    else
        clients_fail=$((clients_fail + 1))
        echo "  FAILED client: $(cat $f)"
    fi
done

avg_latency=$(awk "BEGIN { if ($clients_ok > 0) printf \"%.1f\", $sum_avg / $clients_ok; else print \"N/A\" }")
total_qps=$((total_success * 1000 / (WALL_MS > 0 ? WALL_MS : 1)))
success_rate=$(awk "BEGIN { t=$total_success + $total_fail; if (t > 0) printf \"%.1f\", $total_success * 100 / t; else print \"N/A\" }")

echo ""
echo "=== Results ==="
echo "Wall time:         ${WALL_MS}ms"
echo "Clients OK:        $clients_ok / $CONCURRENT"
echo "Total success:     $total_success"
echo "Total fail:        $total_fail"
echo "Success rate:      ${success_rate}%"
echo "Overall QPS:       ${total_qps} req/s"
echo "Avg latency:       ${avg_latency}ms"
echo "Min latency:       ${overall_min:-N/A}ms"
echo "Max latency:       ${overall_max:-N/A}ms"
echo ""
echo "Per-client reports: $RESULTS_DIR/"
