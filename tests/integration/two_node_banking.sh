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

run_capture() {
  local output
  print_command "$@"
  output="$("$@")"
  if [[ "$VERBOSE" -eq 1 && -n "$output" ]]; then
    print -u2 -- "$output"
  fi
  printf '%s' "$output"
}

if [[ ! -x "$PROVER" ]]; then
  print_command cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml"
  cargo build --manifest-path "$ROOT_DIR/adapters/zk-trace-check/Cargo.toml" >/dev/null
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/etb-banking.XXXXXX")"
verbose_note "tmpdir=$tmpdir"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT

customer_cert="$tmpdir/customer.cert.cbor"
customer_proof="$tmpdir/customer.proof"
teller_cert="$tmpdir/teller.cert.cbor"
teller_proof="$tmpdir/teller.proof"

customer_out="$(run_capture "$BUILD_DIR/etbd" \
  "$ROOT_DIR/examples/banking/customer.etb" \
  'customer says withdrawal_request(tx1001,alice,50)' \
  --cert-out "$customer_cert" \
  --proof-out "$customer_proof" \
  --prover "$PROVER" \
  --verify-proof)"

print -- "$customer_out" | grep -F 'answer 1: customer says withdrawal_request(tx1001,alice,50)' >/dev/null

teller_out="$(run_capture "$BUILD_DIR/etbd" \
  "$ROOT_DIR/examples/banking/teller.etb" \
  'approved(tx1001,alice,50)' \
  --import-cert "$customer_cert" \
  --cert-out "$teller_cert" \
  --proof-out "$teller_proof" \
  --prover "$PROVER" \
  --verify-proof)"

print -- "$teller_out" | grep -F 'answer 1: approved(tx1001,alice,50)' >/dev/null

print_command "$PROVER" verify "$customer_cert" "$customer_proof"
"$PROVER" verify "$customer_cert" "$customer_proof" >/dev/null
print_command "$PROVER" verify "$teller_cert" "$teller_proof"
"$PROVER" verify "$teller_cert" "$teller_proof" >/dev/null

print -- "two-node banking scenario passed"
