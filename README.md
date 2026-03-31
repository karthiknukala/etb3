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

## What Is Distributed Today

The repo now has a live distributed mode built around long-running TCP nodes:

- `etbd serve ... --listen HOST:PORT` starts a node that stays alive and answers
  remote proof queries
- `etbctl query HOST:PORT 'goal(...)'` queries a live node over localhost/TCP
- nodes can be configured with `--peer PRINCIPAL=HOST:PORT`
- when a clause body contains a remote `K says A`, the node can automatically
  query the peer registered for principal `K`, verify the returned proof bundle,
  import the certificate answers, and continue deriving the local result

That is what makes the current prototype distributed: proofs and claims can be
resolved by live nodes over the network instead of exchanging certificate files
manually.

What is still not finished:

- gossip discovery
- heartbeat-based liveness
- TLS transport
- automatic cluster membership propagation

So the live mode is real distributed query execution with static routing, not
yet the full discovery/heartbeat system from the long-term plan.

## Build

```sh
cmake -S . -B build
cmake --build build -j4
```

Build the current proof sidecar when you want proof generation and proof
verification:

```sh
cargo build --manifest-path adapters/zk-trace-check/Cargo.toml
```

## Manual Test Invocation

Run the C test executable directly:

```sh
./build/etb_tests
```

Run the whole CTest suite:

```sh
ctest --test-dir build --output-on-failure
```

Run just the two-node banking integration test:

```sh
ctest --test-dir build -R etb_two_node_banking --output-on-failure
```

Run the banking integration script yourself:

```sh
/bin/zsh tests/integration/two_node_banking.sh "$PWD" "$PWD/build"
```

The integration script assumes:

- you are running from the repository root
- `cmake --build build -j4` has completed
- `cargo build --manifest-path adapters/zk-trace-check/Cargo.toml` has completed

## Writing Your Own Tests

There are two current test styles.

### 1. C unit/integration tests

The existing executable is [`tests/test_main.c`](/Users/e35480/projects/misc/ETB/etb3/tests/test_main.c).
Add a new `static void test_...` function, call it from `main`, then rebuild:

```sh
cmake --build build -j4
./build/etb_tests
```

This is the right place for:

- parser tests
- local engine tests
- delegation/negation tests
- direct capability invocation tests
- snapshot/certificate tests

### 2. Shell integration tests

Put a script under [`tests/integration`](/Users/e35480/projects/misc/ETB/etb3/tests/integration)
and register it with `add_test(...)` in
[`CMakeLists.txt`](/Users/e35480/projects/misc/ETB/etb3/CMakeLists.txt).
The existing banking script is the template:

- [`tests/integration/two_node_banking.sh`](/Users/e35480/projects/misc/ETB/etb3/tests/integration/two_node_banking.sh)

This is the right place for:

- multi-process flows
- certificate import/export tests
- proof generation/verification tests
- end-to-end example scenarios

There is a more detailed test-writing note in
[`tests/README.md`](/Users/e35480/projects/misc/ETB/etb3/tests/README.md).

## Run a Query

```sh
./build/etbd path/to/program.etb 'query(term)'
```

`etbd` also supports:

- `--import-cert FILE` to import answers from another node's certificate
- `--cert-out FILE` to write the CBOR certificate bundle
- `--proof-out FILE` to generate the current end-to-end proof bundle
- `--verify-proof` to verify the generated proof immediately
- `--prover PATH` to select the proof sidecar binary

## Run a Live Node

```sh
./build/etbd serve path/to/program.etb \
  --node-id my-node \
  --listen 127.0.0.1:7601 \
  --peer other_principal=127.0.0.1:7602 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

Query a live node:

```sh
./build/etbctl query 127.0.0.1:7601 'goal(term)' \
  --cert-out /tmp/result.cert.cbor \
  --proof-out /tmp/result.proof \
  --bundle-dir /tmp/result-chain \
  --verify-proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

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

## Example Suite

The examples directory now includes:

- banking: two-node customer/teller certificate exchange
- visa: two-node applicant/consulate certificate exchange
- live-banking: two live nodes that query each other over ports
- live-visa: four live nodes with client -> authority -> delegates
- delegation: transitive `speaks_for`
- negation: stratified negation-as-failure
- tooling: interpreted predicate pipeline using the sample adapters
- temporal: `@ t` and `at T` matching

See [`examples/README.md`](/Users/e35480/projects/misc/ETB/etb3/examples/README.md)
for exact commands for each example.

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

## Current Limits

- The proof sidecar produces a deterministic prototype proof bundle, not a
  production cryptographic ZK proof yet.
- The multi-node examples currently model distributed exchange by importing
  certificate files between separate `etbd` invocations.
- The peer membership, TLS, and registry layers are scaffolded, not production
  complete.
