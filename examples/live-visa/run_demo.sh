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
  TMP_DIR="$(mktemp -d /tmp/etb-live-visa.XXXXXX)"
fi

CHAIN_DIR="$TMP_DIR/chain"
TOP_CERT="$TMP_DIR/client-top.cert.cbor"
TOP_PROOF="$TMP_DIR/client-top.proof"

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
  if [[ "$AUTO_CLEAN" -eq 1 ]]; then
    rm -rf "$TMP_DIR"
  fi
}
trap cleanup EXIT

rm -rf "$CHAIN_DIR"
rm -f /tmp/etb-registry-client.txt /tmp/etb-registry-authority.txt \
  /tmp/etb-registry-payment.txt /tmp/etb-registry-visa.txt

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/authority.etb" \
  --node-id authority \
  --listen 127.0.0.1:7702 \
  --prover "$PROVER" \
  >"$TMP_DIR/authority.log" 2>&1 &
authority_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/payment.etb" \
  --node-id payment \
  --listen 127.0.0.1:7703 \
  --seed 127.0.0.1:7702 \
  --prover "$PROVER" \
  >"$TMP_DIR/payment.log" 2>&1 &
payment_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/visa.etb" \
  --node-id visa \
  --listen 127.0.0.1:7704 \
  --seed 127.0.0.1:7702 \
  --prover "$PROVER" \
  >"$TMP_DIR/visa.log" 2>&1 &
visa_pid=$!

"$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/client.etb" \
  --node-id client \
  --listen 127.0.0.1:7701 \
  --seed 127.0.0.1:7702 \
  --prover "$PROVER" \
  >"$TMP_DIR/client.log" 2>&1 &
client_pid=$!

sleep 2

"$BUILD_DIR/etbctl" query 127.0.0.1:7701 'trip_ready(alice)' \
  --cert-out "$TOP_CERT" \
  --proof-out "$TOP_PROOF" \
  --bundle-dir "$CHAIN_DIR" \
  --verify-proof \
  --prover "$PROVER"

echo "output_dir=$TMP_DIR"
echo "top_certificate=$TOP_CERT"
echo "top_proof=$TOP_PROOF"
echo "bundle_chain=$CHAIN_DIR"
echo "client_log=$TMP_DIR/client.log"
echo "authority_log=$TMP_DIR/authority.log"
echo "payment_log=$TMP_DIR/payment.log"
echo "visa_log=$TMP_DIR/visa.log"
