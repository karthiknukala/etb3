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
  TMP_DIR="$(mktemp -d /tmp/etb-live-visa.XXXXXX)"
fi
verbose_note "tmpdir=$TMP_DIR"

CHAIN_DIR="$TMP_DIR/chain"
TOP_CERT="$TMP_DIR/client-top.cert.cbor"
TOP_PROOF="$TMP_DIR/client-top.proof"

client_pid=""
authority_pid=""
payment_pid=""
visa_pid=""

cleanup() {
  local exit_code=$?
  if [[ "$exit_code" -ne 0 && "$VERBOSE" -eq 1 ]]; then
    if [[ -f "$TMP_DIR/client.log" ]]; then
      print -u2 -- "--- client.log ($TMP_DIR/client.log) ---"
      /usr/bin/sed -n '1,200p' "$TMP_DIR/client.log" >&2 || true
    fi
    if [[ -f "$TMP_DIR/authority.log" ]]; then
      print -u2 -- "--- authority.log ($TMP_DIR/authority.log) ---"
      /usr/bin/sed -n '1,200p' "$TMP_DIR/authority.log" >&2 || true
    fi
    if [[ -f "$TMP_DIR/payment.log" ]]; then
      print -u2 -- "--- payment.log ($TMP_DIR/payment.log) ---"
      /usr/bin/sed -n '1,200p' "$TMP_DIR/payment.log" >&2 || true
    fi
    if [[ -f "$TMP_DIR/visa.log" ]]; then
      print -u2 -- "--- visa.log ($TMP_DIR/visa.log) ---"
      /usr/bin/sed -n '1,200p' "$TMP_DIR/visa.log" >&2 || true
    fi
  fi
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

run_bg_cmd "$TMP_DIR/authority.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/authority.etb" \
  --node-id authority \
  --listen 127.0.0.1:7702 \
  --prover "$PROVER"
authority_pid=$!
verbose_note "authority_pid=$authority_pid"

run_bg_cmd "$TMP_DIR/payment.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/payment.etb" \
  --node-id payment \
  --listen 127.0.0.1:7703 \
  --seed 127.0.0.1:7702 \
  --prover "$PROVER"
payment_pid=$!
verbose_note "payment_pid=$payment_pid"

run_bg_cmd "$TMP_DIR/visa.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/visa.etb" \
  --node-id visa \
  --listen 127.0.0.1:7704 \
  --seed 127.0.0.1:7702 \
  --prover "$PROVER"
visa_pid=$!
verbose_note "visa_pid=$visa_pid"

run_bg_cmd "$TMP_DIR/client.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/client.etb" \
  --node-id client \
  --listen 127.0.0.1:7701 \
  --seed 127.0.0.1:7702 \
  --prover "$PROVER"
client_pid=$!
verbose_note "client_pid=$client_pid"

verbose_note "sleeping for discovery bootstrap"
sleep 2

run_cmd_retry 5 1 "$BUILD_DIR/etbctl" query 127.0.0.1:7701 'trip_ready(alice)' \
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
