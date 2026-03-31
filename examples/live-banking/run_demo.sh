#!/bin/zsh
set -euo pipefail

ROOT_DIR="${1:-$(pwd)}"
BUILD_DIR="${2:-$ROOT_DIR/build}"
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
TMP_DIR="$(mktemp -d /tmp/etb-live-banking.XXXXXX)"

customer_pid=""
teller_pid=""

cleanup() {
  if [[ -n "$customer_pid" ]]; then
    kill "$customer_pid" >/dev/null 2>&1 || true
    wait "$customer_pid" >/dev/null 2>&1 || true
  fi
  if [[ -n "$teller_pid" ]]; then
    kill "$teller_pid" >/dev/null 2>&1 || true
    wait "$teller_pid" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-banking/customer_node.etb" \
  --node-id customer \
  --listen 127.0.0.1:7601 \
  --peer teller=127.0.0.1:7602 \
  --prover "$PROVER" \
  >"$TMP_DIR/customer.log" 2>&1 &
customer_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-banking/teller_node.etb" \
  --node-id teller \
  --listen 127.0.0.1:7602 \
  --peer customer=127.0.0.1:7601 \
  --prover "$PROVER" \
  >"$TMP_DIR/teller.log" 2>&1 &
teller_pid=$!

sleep 1

"$BUILD_DIR/etbctl" query 127.0.0.1:7601 'cash_authorized(tx1001,alice,50)' \
  --cert-out "$TMP_DIR/customer-top.cert.cbor" \
  --proof-out "$TMP_DIR/customer-top.proof" \
  --bundle-dir "$TMP_DIR/chain" \
  --verify-proof \
  --prover "$PROVER"

echo "logs: $TMP_DIR"
