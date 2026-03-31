#!/bin/zsh
set -euo pipefail

usage() {
  print -u2 -- "usage: $0 ROOT_DIR BUILD_DIR [--verbose]"
}

VERBOSE=0

if [[ $# -lt 2 ]]; then
  usage
  exit 1
fi

ROOT_DIR="$1"
BUILD_DIR="$2"
shift 2
PROVER="$ROOT_DIR/adapters/zk-trace-check/target/debug/zk-trace-check"
BASE_PORT=$((8300 + ($$ % 400)))
CLIENT_PORT="$BASE_PORT"
AUTHORITY_PORT="$((BASE_PORT + 1))"
PAYMENT_PORT="$((BASE_PORT + 2))"
VISA_PORT="$((BASE_PORT + 3))"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -v|--verbose)
      VERBOSE=1
      ;;
    *)
      usage
      exit 1
      ;;
  esac
  shift
done

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

run_capture() {
  local output
  print_command "$@"
  output="$("$@")"
  if [[ "$VERBOSE" -eq 1 && -n "$output" ]]; then
    print -u2 -- "$output"
  fi
  printf '%s' "$output"
}

show_log_file() {
  local label="$1"
  local path="$2"
  if [[ "$VERBOSE" -eq 1 && -f "$path" ]]; then
    print -u2 -- "--- $label ($path) ---"
    /usr/bin/sed -n '1,200p' "$path" >&2 || true
  fi
}

run_capture_retry() {
  local attempts="$1"
  local delay_seconds="$2"
  shift 2
  local attempt=1
  local output=""
  local exit_code=0
  while (( attempt <= attempts )); do
    print_command "$@"
    if output="$("$@")"; then
      if [[ "$VERBOSE" -eq 1 && -n "$output" ]]; then
        print -u2 -- "$output"
      fi
      printf '%s' "$output"
      return 0
    fi
    exit_code=$?
    if [[ "$VERBOSE" -eq 1 && -n "$output" ]]; then
      print -u2 -- "$output"
    fi
    if (( attempt < attempts )); then
      verbose_note "query attempt $attempt failed; retrying in ${delay_seconds}s"
      sleep "$delay_seconds"
    fi
    attempt=$((attempt + 1))
  done
  return "$exit_code"
}

if [[ ! -x "$PROVER" ]]; then
  print_command cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml"
  cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml" >/dev/null
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/etb-live-visa.XXXXXX")"
client_pid=""
authority_pid=""
payment_pid=""
visa_pid=""
verbose_note "tmpdir=$tmpdir"

cleanup() {
  local exit_code=$?
  if [[ "$exit_code" -ne 0 ]]; then
    show_log_file "client.log" "$tmpdir/client.log"
    show_log_file "authority.log" "$tmpdir/authority.log"
    show_log_file "payment.log" "$tmpdir/payment.log"
    show_log_file "visa.log" "$tmpdir/visa.log"
  fi
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

run_bg_cmd "$tmpdir/authority.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/authority.etb" \
  --node-id authority \
  --listen "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER"
authority_pid=$!
verbose_note "authority_pid=$authority_pid"

run_bg_cmd "$tmpdir/payment.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/payment.etb" \
  --node-id payment \
  --listen "127.0.0.1:$PAYMENT_PORT" \
  --seed "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER"
payment_pid=$!
verbose_note "payment_pid=$payment_pid"

run_bg_cmd "$tmpdir/visa.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/visa.etb" \
  --node-id visa \
  --listen "127.0.0.1:$VISA_PORT" \
  --seed "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER"
visa_pid=$!
verbose_note "visa_pid=$visa_pid"

run_bg_cmd "$tmpdir/client.log" \
  "$BUILD_DIR/etbd" serve "$ROOT_DIR/examples/live-visa/client.etb" \
  --node-id client \
  --listen "127.0.0.1:$CLIENT_PORT" \
  --seed "127.0.0.1:$AUTHORITY_PORT" \
  --prover "$PROVER"
client_pid=$!
verbose_note "client_pid=$client_pid"

verbose_note "sleeping for discovery bootstrap"
sleep 3

query_out="$(run_capture_retry 10 1 "$BUILD_DIR/etbctl" query "127.0.0.1:$CLIENT_PORT" \
  'trip_ready(alice)' \
  --cert-out "$tmpdir/client-top.cert.cbor" \
  --proof-out "$tmpdir/client-top.proof" \
  --bundle-dir "$tmpdir/chain" \
  --verify-proof \
  --prover "$PROVER")"

print -- "$query_out" | grep -F 'answer 1: trip_ready(alice)' >/dev/null
print -- "$query_out" | grep -F 'bundles=4' >/dev/null
print_command "$PROVER" verify "$tmpdir/client-top.cert.cbor" "$tmpdir/client-top.proof"
"$PROVER" verify "$tmpdir/client-top.cert.cbor" "$tmpdir/client-top.proof" >/dev/null

proof_count="$(find "$tmpdir/chain" -name '*.proof' | wc -l | tr -d ' ')"
verbose_note "proof_count=$proof_count"
if [[ "$proof_count" != "4" ]]; then
  echo "expected 4 proof files, found $proof_count" >&2
  exit 1
fi

print -- "live seed visa scenario passed"
