#!/bin/zsh
set -euo pipefail

ROOT_DIR="${1:-$(pwd)}"
BUILD_DIR="${2:-$ROOT_DIR/build}"
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
TMP_DIR="$(mktemp -d /tmp/etb-live-visa.XXXXXX)"

client_pid=""
authority_pid=""
payment_pid=""
visa_pid=""

cleanup() {
  for pid in "$client_pid" "$authority_pid" "$payment_pid" "$visa_pid"; do
    if [[ -n "$pid" ]]; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" >/dev/null 2>&1 || true
    fi
  done
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/client.etb" \
  --node-id client \
  --listen 127.0.0.1:7701 \
  --peer authority=127.0.0.1:7702 \
  --prover "$PROVER" \
  >"$TMP_DIR/client.log" 2>&1 &
client_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/authority.etb" \
  --node-id authority \
  --listen 127.0.0.1:7702 \
  --peer payment=127.0.0.1:7703 \
  --peer visa=127.0.0.1:7704 \
  --prover "$PROVER" \
  >"$TMP_DIR/authority.log" 2>&1 &
authority_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/payment.etb" \
  --node-id payment \
  --listen 127.0.0.1:7703 \
  --prover "$PROVER" \
  >"$TMP_DIR/payment.log" 2>&1 &
payment_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/visa.etb" \
  --node-id visa \
  --listen 127.0.0.1:7704 \
  --prover "$PROVER" \
  >"$TMP_DIR/visa.log" 2>&1 &
visa_pid=$!

sleep 1

"$BUILD_DIR/etbctl" query 127.0.0.1:7701 'trip_ready(alice)' \
  --cert-out "$TMP_DIR/client-top.cert.cbor" \
  --proof-out "$TMP_DIR/client-top.proof" \
  --bundle-dir "$TMP_DIR/chain" \
  --verify-proof \
  --prover "$PROVER"

echo "logs: $TMP_DIR"
