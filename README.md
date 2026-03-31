# etb3

Peer-to-peer Evidential Tool Bus prototype in C11 with:

- stratified Datalog
- `says`
- `speaks_for`
- interpreted predicates via subprocess adapters
- deterministic trace and certificate export
- SWIM-style membership scaffolding
- snapshot/signer scaffolding
- Rust ZK sidecar skeleton

## Build

```sh
cmake -S . -B build
cmake --build build -j4
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Run

```sh
./build/etbd path/to/program.etb 'query(term)'
```

`etbd` also supports:

- `--import-cert FILE` to import answers from another node's certificate
- `--cert-out FILE` to write the CBOR certificate bundle
- `--proof-out FILE` to generate the current end-to-end proof bundle
- `--verify-proof` to verify the generated proof immediately
- `--prover PATH` to select the proof sidecar binary

## Included Sample Adapters

- `sample_concat`: deterministic capability returning a concatenated string
- `sample_receipt`: evidence-producing capability returning a digest receipt

## ZK Sidecar

The Rust sidecar lives in [`adapters/zk-trace-check`](adapters/zk-trace-check) and
currently provides deterministic placeholder commands for:

- `segment-prove`
- `fold`
- `prove`
- `verify`

`prove` emits the current full proof bundle for a certificate, and `verify`
checks that proof bundle against the certificate. This is the current prototype
proof flow for zk-ETB; the sidecar is still a deterministic placeholder rather
than a production cryptographic prover.

## Two-Node Banking Example

This repo includes a customer node and a teller node:

- customer program: [examples/banking/customer.etb](/Users/e35480/projects/misc/ETB/etb3/examples/banking/customer.etb)
- teller program: [examples/banking/teller.etb](/Users/e35480/projects/misc/ETB/etb3/examples/banking/teller.etb)

Build both the C binaries and the proof sidecar:

```sh
cmake -S . -B build
cmake --build build -j4
cargo build --manifest-path adapters/zk-trace-check/Cargo.toml
```

Run the customer node to emit a certificate and full proof bundle for the
withdrawal request:

```sh
./build/etbd \
  examples/banking/customer.etb \
  'customer says withdrawal_request(tx1001,alice,50)' \
  --cert-out /tmp/customer.cert.cbor \
  --proof-out /tmp/customer.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

Run the teller node, import the customer's certificate, derive approval, and
emit its own proof bundle:

```sh
./build/etbd \
  examples/banking/teller.etb \
  'approved(tx1001,alice,50)' \
  --import-cert /tmp/customer.cert.cbor \
  --cert-out /tmp/teller.cert.cbor \
  --proof-out /tmp/teller.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

You can also verify either proof bundle directly:

```sh
./adapters/zk-trace-check/target/debug/zk-trace-check verify \
  /tmp/customer.cert.cbor \
  /tmp/customer.proof
```

The same flow is exercised automatically by the integration test
`etb_two_node_banking`.
