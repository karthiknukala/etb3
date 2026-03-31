# Tests

The repo currently uses two test layers:

- C tests in [`test_main.c`](/Users/e35480/projects/misc/ETB/etb3/tests/test_main.c)
- shell integration tests in
  [`integration`](/Users/e35480/projects/misc/ETB/etb3/tests/integration)

## Running Tests

From the repo root:

```sh
cmake -S . -B build
cmake --build build -j4
./build/etb_tests
ctest --test-dir build --output-on-failure
```

For proof-generating integration tests, also build the Rust sidecar:

```sh
cargo build --manifest-path adapters/zk-trace-check/Cargo.toml
```

Run one integration script directly:

```sh
/bin/zsh tests/integration/two_node_banking.sh "$PWD" "$PWD/build"
```

Verbose mode:

```sh
/bin/zsh tests/integration/two_node_banking.sh "$PWD" "$PWD/build" --verbose
```

Run the live discovery integrations directly:

```sh
/bin/zsh tests/integration/live_seed_banking.sh "$PWD" "$PWD/build"
```

```sh
/bin/zsh tests/integration/live_seed_visa.sh "$PWD" "$PWD/build"
```

Each shell script also accepts `--verbose` to print commands, temp directories,
and captured node logs on failure.

## Adding a C Test

1. Add a new `static void test_name(void)` function to
   [`test_main.c`](/Users/e35480/projects/misc/ETB/etb3/tests/test_main.c).
2. Use `expect_true(...)` for assertions.
3. Call the new function from `main`.
4. Rebuild and run `./build/etb_tests`.

Good fits for this layer:

- parsing and canonicalization
- fixpoint evaluation
- stratified negation
- `says` and `speaks_for`
- capability invocation without separate processes
- certificate and snapshot helpers

## Adding an Integration Test

1. Create a shell script under
   [`integration`](/Users/e35480/projects/misc/ETB/etb3/tests/integration).
2. Follow the pattern in
   [`two_node_banking.sh`](/Users/e35480/projects/misc/ETB/etb3/tests/integration/two_node_banking.sh):
   accept `SOURCE_DIR` and `BUILD_DIR` as positional arguments, use `set -euo pipefail`,
   support `--verbose`, and fail loudly on missing output.
3. Register it in [`CMakeLists.txt`](/Users/e35480/projects/misc/ETB/etb3/CMakeLists.txt)
   with `add_test(...)`.
4. Run it with `ctest --test-dir build -R <name> --output-on-failure`.

Good fits for this layer:

- multi-node certificate exchange
- seed-based service discovery
- proof generation and verification
- subprocess adapter workflows
- CLI regression tests

## Practical Advice

- Keep example programs under [`examples`](/Users/e35480/projects/misc/ETB/etb3/examples)
  and reuse them in integration tests when possible.
- When you add a new adapter, add a small unit-style capability test first, then
  an end-to-end shell test if the adapter matters to a workflow.
- If a test depends on the proof sidecar, document that it exercises the current
  proof relation and what statement is actually being verified.
