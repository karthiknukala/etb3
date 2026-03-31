# Examples

These examples are designed for the current prototype, so the commands below
assume:

- you are running from the repository root
- the C binaries have been built in `./build`
- proof-generating examples also have the Rust sidecar built

Build everything once:

```sh
cmake -S . -B build
cmake --build build -j4
cargo build --manifest-path adapters/zk-trace-check/Cargo.toml
```

## 1. Banking: Customer and Teller

Files:

- [`banking/customer.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/banking/customer.etb)
- [`banking/teller.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/banking/teller.etb)

This is the same two-node scenario used by the integration test. The customer
emits a certificate for a withdrawal request, and the teller imports it to
approve the transaction.

```sh
./build/etbd \
  examples/banking/customer.etb \
  'customer says withdrawal_request(tx1001,alice,50)' \
  --cert-out /tmp/customer.cert.cbor \
  --proof-out /tmp/customer.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

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

## 2. Visa: Applicant and Consulate

Files:

- [`visa/applicant.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/visa/applicant.etb)
- [`visa/consulate.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/visa/consulate.etb)

This is a Cyberlogic-style authorization flow. The applicant publishes two
attested claims, and the consulate combines them to derive a visa approval.

```sh
./build/etbd \
  examples/visa/applicant.etb \
  'applicant says passport_valid(alice)' \
  --cert-out /tmp/visa-passport.cert.cbor \
  --proof-out /tmp/visa-passport.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

```sh
./build/etbd \
  examples/visa/applicant.etb \
  'applicant says sufficient_funds(alice)' \
  --cert-out /tmp/visa-funds.cert.cbor \
  --proof-out /tmp/visa-funds.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

```sh
./build/etbd \
  examples/visa/consulate.etb \
  'visa_approved(alice)' \
  --import-cert /tmp/visa-passport.cert.cbor \
  --import-cert /tmp/visa-funds.cert.cbor \
  --cert-out /tmp/visa-approval.cert.cbor \
  --proof-out /tmp/visa-approval.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

## 3. Delegation: Transitive `speaks_for`

File:

- [`delegation/transitive_bank.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/delegation/transitive_bank.etb)

This shows a transitive delegation chain: `alice speaks_for manager` and
`manager speaks_for bank`, so `alice says approve(loan42)` can satisfy
`bank says approve(loan42)`.

```sh
./build/etbd \
  examples/delegation/transitive_bank.etb \
  'bank says approve(loan42)' \
  --cert-out /tmp/delegation.cert.cbor \
  --proof-out /tmp/delegation.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

## 4. Stratified Negation

File:

- [`negation/stratified_access.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/negation/stratified_access.etb)

This is the smallest local example showing `not` in the supported stratified
fragment.

Positive query:

```sh
./build/etbd examples/negation/stratified_access.etb 'allowed(alice)'
```

Negative query:

```sh
./build/etbd examples/negation/stratified_access.etb 'allowed(bob)'
```

The second command should return no `answer ...` lines.

## 5. Interpreted Predicates and Evidence

File:

- [`tooling/tool_chain.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/tooling/tool_chain.etb)

This is a toy ETB tool-bus workflow. It first calls the `sample_concat`
adapter, then feeds the result into the `sample_receipt` adapter.

```sh
./build/etbd \
  examples/tooling/tool_chain.etb \
  'receipted("withdrawal:alice")' \
  --cert-out /tmp/tool-chain.cert.cbor \
  --proof-out /tmp/tool-chain.proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check \
  --verify-proof
```

## 6. Temporal Facts

File:

- [`temporal/policy_windows.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/temporal/policy_windows.etb)

This shows the current prototype handling for `@ t` and `at T`.

```sh
./build/etbd examples/temporal/policy_windows.etb 'usable_clearance(alice)'
```

```sh
./build/etbd examples/temporal/policy_windows.etb 'anchored_badge(alice)'
```

Notes:

- `@ t` is checked against the local wall clock.
- `at T` is currently syntactic/exact-match behavior in the prototype rather
  than a real blockchain oracle check.

## Direct Proof Verification

Any example that emits both `--cert-out` and `--proof-out` can be checked again
with the sidecar:

```sh
./adapters/zk-trace-check/target/debug/zk-trace-check verify \
  /tmp/delegation.cert.cbor \
  /tmp/delegation.proof
```

The current sidecar is still a deterministic prototype proof bundle verifier,
not a production cryptographic ZK prover.

## 7. Live Distributed Banking

Files:

- [`live-banking/customer_node.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/live-banking/customer_node.etb)
- [`live-banking/teller_node.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/live-banking/teller_node.etb)
- [`live-banking/run_demo.sh`](/Users/e35480/projects/misc/ETB/etb3/examples/live-banking/run_demo.sh)

This is the first real live-network example. Two nodes stay up on ports:

- the customer node holds the withdrawal request and asks the teller for approval
- the teller node calls back to the customer node for `customer says withdrawal_request(...)`
- the final client-visible result is produced by querying the customer node

Start the nodes in separate terminals:

```sh
./build/etbd serve examples/live-banking/customer_node.etb \
  --node-id customer \
  --listen 127.0.0.1:7601 \
  --peer teller=127.0.0.1:7602 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

```sh
./build/etbd serve examples/live-banking/teller_node.etb \
  --node-id teller \
  --listen 127.0.0.1:7602 \
  --peer customer=127.0.0.1:7601 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

Then query the live customer node:

```sh
./build/etbctl query 127.0.0.1:7601 'cash_authorized(tx1001,alice,50)' \
  --cert-out /tmp/customer-live.cert.cbor \
  --proof-out /tmp/customer-live.proof \
  --bundle-dir /tmp/customer-live-chain \
  --verify-proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

Expected result:

- answer: `cash_authorized(tx1001,alice,50)`
- bundle chain contains three proofs:
  - customer withdrawal request
  - teller approval
  - customer final authorization

There is also a helper script:

```sh
/bin/zsh examples/live-banking/run_demo.sh "$PWD" "$PWD/build"
```

Persist the generated proofs and certificates in a directory you choose:

```sh
/bin/zsh examples/live-banking/run_demo.sh "$PWD" "$PWD/build" \
  --out-dir "$PWD/demo-output/live-banking"
```

That directory will contain:

- `customer-top.cert.cbor`
- `customer-top.proof`
- `chain/`
- `customer.log`
- `teller.log`

## 8. Live Four-Node Visa Chain

Files:

- [`live-visa/client.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/live-visa/client.etb)
- [`live-visa/authority.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/live-visa/authority.etb)
- [`live-visa/payment.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/live-visa/payment.etb)
- [`live-visa/visa.etb`](/Users/e35480/projects/misc/ETB/etb3/examples/live-visa/visa.etb)
- [`live-visa/run_demo.sh`](/Users/e35480/projects/misc/ETB/etb3/examples/live-visa/run_demo.sh)

This is the coalesced four-node example you asked for:

- client node queries authority
- authority node queries the payment node and the visa node
- all nodes stay alive on ports and can answer new queries
- the final response includes a proof chain for every hop

Start the four nodes in separate terminals:

```sh
./build/etbd serve examples/live-visa/client.etb \
  --node-id client \
  --listen 127.0.0.1:7701 \
  --peer authority=127.0.0.1:7702 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

```sh
./build/etbd serve examples/live-visa/authority.etb \
  --node-id authority \
  --listen 127.0.0.1:7702 \
  --peer payment=127.0.0.1:7703 \
  --peer visa=127.0.0.1:7704 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

```sh
./build/etbd serve examples/live-visa/payment.etb \
  --node-id payment \
  --listen 127.0.0.1:7703 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

```sh
./build/etbd serve examples/live-visa/visa.etb \
  --node-id visa \
  --listen 127.0.0.1:7704 \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

Then query the live client node:

```sh
./build/etbctl query 127.0.0.1:7701 'trip_ready(alice)' \
  --cert-out /tmp/live-visa.cert.cbor \
  --proof-out /tmp/live-visa.proof \
  --bundle-dir /tmp/live-visa-chain \
  --verify-proof \
  --prover ./adapters/zk-trace-check/target/debug/zk-trace-check
```

Expected result:

- answer: `trip_ready(alice)`
- bundle chain contains four proofs:
  - payment
  - visa
  - authority
  - client

There is also a helper script:

```sh
/bin/zsh examples/live-visa/run_demo.sh "$PWD" "$PWD/build"
```

Persist the generated proofs and certificates in a directory you choose:

```sh
/bin/zsh examples/live-visa/run_demo.sh "$PWD" "$PWD/build" \
  --out-dir "$PWD/demo-output/live-visa"
```

That directory will contain:

- `client-top.cert.cbor`
- `client-top.proof`
- `chain/`
- `client.log`
- `authority.log`
- `payment.log`
- `visa.log`

## Notes On The Live Mode

- Live nodes currently use static `--peer PRINCIPAL=HOST:PORT` routing.
- They automatically verify incoming proof bundles before importing answers
  when you configure `--prover`.
- The transport is plain TCP in this prototype. TLS and gossip discovery are
  still future work.
