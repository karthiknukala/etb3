#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(pwd)"
BUILD_DIR=""
OUT_DIR=""
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
AUTO_CLEAN=1
VERBOSE=0

usage() {
  print -u2 -- "usage: $0 [ROOT_DIR] [BUILD_DIR] [--out-dir DIR] [--verbose]"
}

verbose_note() {
  if [[ "$VERBOSE" -eq 1 ]]; then
    print -u2 -- "$*"
  fi
}

print_command() {
  local arg
  if [[ "$VERBOSE" -eq 1 ]]; then
    printf '+' >&2
    for arg in "$@"; do
      printf ' %q' "$arg" >&2
    done
    printf '\n' >&2
  fi
}

run_bg_cmd() {
  local log_file="$1"
  shift
  print_command "$@"
  verbose_note "  log=$log_file"
  "$@" >"$log_file" 2>&1 &
}

run_cmd() {
  print_command "$@"
  "$@"
}

run_cmd_retry() {
  local attempts="$1"
  local delay_seconds="$2"
  shift 2
  local attempt=1
  while (( attempt <= attempts )); do
    print_command "$@"
    if "$@"; then
      return 0
    fi
    if (( attempt < attempts )); then
      verbose_note "query attempt $attempt failed; retrying in ${delay_seconds}s"
      sleep "$delay_seconds"
    fi
    attempt=$((attempt + 1))
  done
  return 1
}

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
        usage
        exit 1
      fi
      OUT_DIR="$2"
      shift 2
      ;;
    -v|--verbose)
      VERBOSE=1
      shift
      ;;
    *)
      usage
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
verbose_note "tmpdir=$TMP_DIR"

CHAIN_DIR="$TMP_DIR/chain"
TOP_CERT="$TMP_DIR/customer-top.cert.cbor"
TOP_PROOF="$TMP_DIR/customer-top.proof"

customer_pid=""
teller_pid=""

cleanup() {
  local exit_code=$?
  if [[ "$exit_code" -ne 0 && "$VERBOSE" -eq 1 ]]; then
    if [[ -f "$TMP_DIR/customer.log" ]]; then
      print -u2 -- "--- customer.log ($TMP_DIR/customer.log) ---"
      /usr/bin/sed -n '1,200p' "$TMP_DIR/customer.log" >&2 || true
    fi
    if [[ -f "$TMP_DIR/teller.log" ]]; then
      print -u2 -- "--- teller.log ($TMP_DIR/teller.log) ---"
      /usr/bin/sed -n '1,200p' "$TMP_DIR/teller.log" >&2 || true
    fi
  fi
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
rm -f /tmp/etb-registry-customer.txt /tmp/etb-registry-teller.txt

run_bg_cmd "$TMP_DIR/customer.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-banking/customer_node.etb" \
  --node-id customer \
  --listen 127.0.0.1:7601 \
  --seed 127.0.0.1:7602 \
  --prover "$PROVER"
customer_pid=$!
verbose_note "customer_pid=$customer_pid"

run_bg_cmd "$TMP_DIR/teller.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-banking/teller_node.etb" \
  --node-id teller \
  --listen 127.0.0.1:7602 \
  --seed 127.0.0.1:7601 \
  --prover "$PROVER"
teller_pid=$!
verbose_note "teller_pid=$teller_pid"

verbose_note "sleeping for discovery bootstrap"
sleep 2

run_cmd_retry 5 1 "$BUILD_DIR/etbctl" query 127.0.0.1:7601 'cash_authorized(tx1001,alice,50)' \
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
