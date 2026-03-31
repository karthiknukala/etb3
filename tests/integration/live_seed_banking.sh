#!/bin/zsh
set -euo pipefail

ROOT_DIR="$1"
BUILD_DIR="$2"
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
BASE_PORT=$((8200 + ($$ % 400)))
CUSTOMER_PORT="$BASE_PORT"
TELLER_PORT="$((BASE_PORT + 1))"

if [[ ! -x "$PROVER" ]]; then
  cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml" >/dev/null
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/etb-live-banking.XXXXXX")"
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
  rm -rf "$tmpdir"
  rm -f /tmp/etb-registry-customer.txt /tmp/etb-registry-teller.txt
}
trap cleanup EXIT

rm -f /tmp/etb-registry-customer.txt /tmp/etb-registry-teller.txt

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-banking/customer_node.etb" \
  --node-id customer \
  --listen "127.0.0.1:$CUSTOMER_PORT" \
  --seed "127.0.0.1:$TELLER_PORT" \
  --prover "$PROVER" \
  >"$tmpdir/customer.log" 2>&1 &
customer_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-banking/teller_node.etb" \
  --node-id teller \
  --listen "127.0.0.1:$TELLER_PORT" \
  --seed "127.0.0.1:$CUSTOMER_PORT" \
  --prover "$PROVER" \
  >"$tmpdir/teller.log" 2>&1 &
teller_pid=$!

sleep 2

query_out="$("$BUILD_DIR/etbctl" query "127.0.0.1:$CUSTOMER_PORT" \
  'cash_authorized(tx1001,alice,50)' \
  --cert-out "$tmpdir/customer-top.cert.cbor" \
  --proof-out "$tmpdir/customer-top.proof" \
  --bundle-dir "$tmpdir/chain" \
  --verify-proof \
  --prover "$PROVER")"

print -- "$query_out" | grep -F 'answer 1: cash_authorized(tx1001,alice,50)' >/dev/null
print -- "$query_out" | grep -F 'bundles=3' >/dev/null
"$PROVER" verify "$tmpdir/customer-top.cert.cbor" "$tmpdir/customer-top.proof" >/dev/null

proof_count="$(find "$tmpdir/chain" -name '*.proof' | wc -l | tr -d ' ')"
if [[ "$proof_count" != "3" ]]; then
  echo "expected 3 proof files, found $proof_count" >&2
  exit 1
fi

print -- "live seed banking scenario passed"
