#!/bin/zsh
set -euo pipefail

ROOT_DIR="$1"
BUILD_DIR="$2"
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
BASE_PORT=$((8300 + ($$ % 400)))
CLIENT_PORT="$BASE_PORT"
AUTHORITY_PORT="$((BASE_PORT + 1))"
PAYMENT_PORT="$((BASE_PORT + 2))"
VISA_PORT="$((BASE_PORT + 3))"

if [[ ! -x "$PROVER" ]]; then
  cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml" >/dev/null
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/etb-live-visa.XXXXXX")"
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
  rm -rf "$tmpdir"
  rm -f /tmp/etb-registry-client.txt /tmp/etb-registry-authority.txt \
    /tmp/etb-registry-payment.txt /tmp/etb-registry-visa.txt
}
trap cleanup EXIT

rm -f /tmp/etb-registry-client.txt /tmp/etb-registry-authority.txt \
  /tmp/etb-registry-payment.txt /tmp/etb-registry-visa.txt

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/authority.etb" \
  --node-id authority \
  --listen "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER" \
  >"$tmpdir/authority.log" 2>&1 &
authority_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/payment.etb" \
  --node-id payment \
  --listen "127.0.0.1:$PAYMENT_PORT" \
  --seed "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER" \
  >"$tmpdir/payment.log" 2>&1 &
payment_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/visa.etb" \
  --node-id visa \
  --listen "127.0.0.1:$VISA_PORT" \
  --seed "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER" \
  >"$tmpdir/visa.log" 2>&1 &
visa_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/client.etb" \
  --node-id client \
  --listen "127.0.0.1:$CLIENT_PORT" \
  --seed "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER" \
  >"$tmpdir/client.log" 2>&1 &
client_pid=$!

sleep 2

query_out="$("$BUILD_DIR/etbctl" query "127.0.0.1:$CLIENT_PORT" \
  'trip_ready(alice)' \
  --cert-out "$tmpdir/client-top.cert.cbor" \
  --proof-out "$tmpdir/client-top.proof" \
  --bundle-dir "$tmpdir/chain" \
  --verify-proof \
  --prover "$PROVER")"

print -- "$query_out" | grep -F 'answer 1: trip_ready(alice)' >/dev/null
print -- "$query_out" | grep -F 'bundles=4' >/dev/null
"$PROVER" verify "$tmpdir/client-top.cert.cbor" "$tmpdir/client-top.proof" >/dev/null

proof_count="$(find "$tmpdir/chain" -name '*.proof' | wc -l | tr -d ' ')"
if [[ "$proof_count" != "4" ]]; then
  echo "expected 4 proof files, found $proof_count" >&2
  exit 1
fi

print -- "live seed visa scenario passed"
