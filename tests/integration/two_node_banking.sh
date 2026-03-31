#!/bin/zsh
set -euo pipefail

ROOT_DIR="$1"
BUILD_DIR="$2"
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"

if [[ ! -x "$PROVER" ]]; then
  cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml" >/dev/null
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/etb-banking.XXXXXX")"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT

customer_cert="$tmpdir/customer.cert.cbor"
customer_proof="$tmpdir/customer.proof"
teller_cert="$tmpdir/teller.cert.cbor"
teller_proof="$tmpdir/teller.proof"

customer_out="$("$BUILD_DIR/etbd" \
  "$ROOT_DIR/examples/banking/customer.etb" \
  'customer says withdrawal_request(tx1001,alice,50)' \
  --cert-out "$customer_cert" \
  --proof-out "$customer_proof" \
  --prover "$PROVER" \
  --verify-proof)"

print -- "$customer_out" | grep -F 'answer 1: customer says withdrawal_request(tx1001,alice,50)' >/dev/null

teller_out="$("$BUILD_DIR/etbd" \
  "$ROOT_DIR/examples/banking/teller.etb" \
  'approved(tx1001,alice,50)' \
  --import-cert "$customer_cert" \
  --cert-out "$teller_cert" \
  --proof-out "$teller_proof" \
  --prover "$PROVER" \
  --verify-proof)"

print -- "$teller_out" | grep -F 'answer 1: approved(tx1001,alice,50)' >/dev/null

"$PROVER" verify "$customer_cert" "$customer_proof" >/dev/null
"$PROVER" verify "$teller_cert" "$teller_proof" >/dev/null

print -- "two-node banking scenario passed"
