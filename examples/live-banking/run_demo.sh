#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(pwd)"
BUILD_DIR=""
OUT_DIR=""
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
AUTO_CLEAN=1

if [[ $# -gt 0 && "${1#--}" == "$1" ]]; then
  ROOT_DIR="$1"
  shift
fi
if [[ $# -gt 0 && "${1#--}" == "$1" ]]; then
  BUILD_DIR="$1"
  shift
else
  BUILD_DIR="$ROOT_DIR/build"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out-dir)
      if [[ $# -lt 2 ]]; then
        echo "usage: $0 [ROOT_DIR] [BUILD_DIR] [--out-dir DIR]" >&2
        exit 1
      fi
      OUT_DIR="$2"
      shift 2
      ;;
    *)
      echo "usage: $0 [ROOT_DIR] [BUILD_DIR] [--out-dir DIR]" >&2
      exit 1
      ;;
  esac
done

if [[ -n "$OUT_DIR" ]]; then
  mkdir -p "$OUT_DIR"
  TMP_DIR="$OUT_DIR"
  AUTO_CLEAN=0
else
  TMP_DIR="$(mktemp -d /tmp/etb-live-banking.XXXXXX)"
fi

CHAIN_DIR="$TMP_DIR/chain"
TOP_CERT="$TMP_DIR/customer-top.cert.cbor"
TOP_PROOF="$TMP_DIR/customer-top.proof"

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
  if [[ "$AUTO_CLEAN" -eq 1 ]]; then
    rm -rf "$TMP_DIR"
  fi
}
trap cleanup EXIT

rm -rf "$CHAIN_DIR"

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
  --cert-out "$TOP_CERT" \
  --proof-out "$TOP_PROOF" \
  --bundle-dir "$CHAIN_DIR" \
  --verify-proof \
  --prover "$PROVER"

echo "output_dir=$TMP_DIR"
echo "top_certificate=$TOP_CERT"
echo "top_proof=$TOP_PROOF"
echo "bundle_chain=$CHAIN_DIR"
echo "customer_log=$TMP_DIR/customer.log"
echo "teller_log=$TMP_DIR/teller.log"
