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

## Included Sample Adapters

- `sample_concat`: deterministic capability returning a concatenated string
- `sample_receipt`: evidence-producing capability returning a digest receipt

## ZK Sidecar

The Rust sidecar lives in [`adapters/zk-trace-check`](adapters/zk-trace-check) and
currently provides deterministic placeholder commands for:

- `segment-prove`
- `fold`
- `verify`
