# Architecture And Communication Flows

This page documents the current zk-ETB prototype as it exists in this repo:

- C11 ETB nodes running as long-lived TCP services
- seed-based route discovery and registry exchange
- distributed resolution of `K says A` subgoals
- deterministic certificate export
- a Rust proof sidecar for certificate-bound proof generation and verification

The diagrams below describe the current implementation, not the eventual
heartbeat/TLS/signed-discovery design.

## System Architecture

```mermaid
flowchart LR
  User["Operator / etbctl"]
  Verifier["Independent verifier<br/>zk-trace-check verify"]

  subgraph Cluster["zk-ETB Cluster (seed-discovered peers)"]
    direction LR

    subgraph ClientNode["Client / Customer Node"]
      ClientRPC["TCP RPC listener"]
      ClientEngine["ETB engine<br/>Datalog + says + speaks_for + negation"]
      ClientTrace["Trace + certificate builder"]
      ClientRegistry["Local route registry cache"]
      ClientArtifacts["Artifact / bundle store"]
      ClientProver["Rust proof sidecar"]
      ClientRPC --> ClientEngine
      ClientEngine --> ClientTrace
      ClientEngine --> ClientRegistry
      ClientTrace --> ClientArtifacts
      ClientTrace --> ClientProver
    end

    subgraph AuthorityNode["Authority / Teller Node"]
      AuthorityRPC["TCP RPC listener"]
      AuthorityEngine["ETB engine"]
      AuthorityTrace["Trace + certificate builder"]
      AuthorityRegistry["Local route registry cache"]
      AuthorityArtifacts["Artifact / bundle store"]
      AuthorityProver["Rust proof sidecar"]
      AuthorityRPC --> AuthorityEngine
      AuthorityEngine --> AuthorityTrace
      AuthorityEngine --> AuthorityRegistry
      AuthorityTrace --> AuthorityArtifacts
      AuthorityTrace --> AuthorityProver
    end

    subgraph DelegateNode["Delegate / Capability Node"]
      DelegateRPC["TCP RPC listener"]
      DelegateEngine["ETB engine"]
      DelegateTrace["Trace + certificate builder"]
      DelegateRegistry["Local route registry cache"]
      DelegateCapability["Interpreted predicate adapters"]
      DelegateProver["Rust proof sidecar"]
      DelegateRPC --> DelegateEngine
      DelegateEngine --> DelegateTrace
      DelegateEngine --> DelegateRegistry
      DelegateEngine --> DelegateCapability
      DelegateTrace --> DelegateProver
    end
  end

  User -->|submit query| ClientRPC
  ClientEngine -->|resolve remote says goals| AuthorityRPC
  AuthorityEngine -->|resolve remote says / tool goals| DelegateRPC
  ClientRPC -->|bundles + certificate + proof chain| User
  User -->|saved cert/proof files| Verifier
```

## Query Lifecycle Phases

```mermaid
flowchart TD
  Start["1. Node startup"] --> Load["Load .etb program,<br/>discover local principals,<br/>initialize local registry cache"]
  Load --> Seed["2. Seed discovery exchange<br/>announce local routes,<br/>pull registry snapshots"]
  Seed --> Query["3. Query admission<br/>parse goal and create evaluation state"]
  Query --> Eval["4. Local evaluation<br/>stratified rules, negation, says, speaks_for"]
  Eval --> Remote{"Remote subgoal<br/>needed?"}
  Remote -- "No" --> Cert["6. Build certificate bundle<br/>from answers and trace"]
  Remote -- "Yes" --> Resolve["5. Resolve principal/capability<br/>through local registry cache"]
  Resolve --> RPC["Fetch remote certificate bundle(s)<br/>from live peer"]
  RPC --> Verify["Verify imported bundle(s)<br/>before adding facts"]
  Verify --> Eval
  Eval --> Temporal["Temporal checks<br/>@ t against local clock<br/>at T as exact anchor tag match"]
  Temporal --> Cert
  Cert --> Proof["7. Generate proof file<br/>with Rust sidecar"]
  Proof --> Return["8. Return top-level cert/proof<br/>and optional chain directory"]
  Return --> Independent["9. Independent verification<br/>against saved cert/proof files"]
```

## Sequence: Seed Discovery Bootstrap

This is the current best-effort discovery path. A node learns routes from seed
peers and from later query traffic.

```mermaid
sequenceDiagram
  participant A as Seed peer / authority node
  participant P as Payment node
  participant V as Visa node
  participant C as Client node

  A->>A: Start listener and initialize local route cache
  P->>P: Load local program and infer hosted principals
  P->>A: announce(node=payment, routes=[payment -> host:port])
  A-->>P: registry snapshot(routes known so far)
  V->>V: Load local program and infer hosted principals
  V->>A: announce(node=visa, routes=[visa -> host:port])
  A-->>V: registry snapshot(routes += visa)
  C->>C: Load local program and infer hosted principals
  C->>A: announce(node=client, routes=[client -> host:port])
  A-->>C: registry snapshot(routes += client,payment,visa)
  Note over A,C: Later query and resolve RPCs also piggyback route snapshots.<br/>Discovery keeps converging as traffic flows.
```

## Sequence: Two-Node Banking Query

This is the live customer/teller example.

```mermaid
sequenceDiagram
  participant U as Operator / etbctl
  participant C as Customer node
  participant ZC as Customer proof sidecar
  participant T as Teller node
  participant ZT as Teller proof sidecar

  U->>C: query cash_authorized(tx1001,alice,50)
  C->>C: Evaluate local rule requiring teller says approved(...)
  C->>T: remote query for teller says approved(...)
  T->>T: Evaluate teller rule
  T->>C: remote query for customer says withdrawal_request(...)
  C->>C: Derive attested withdrawal request
  C->>ZC: prove(customer withdrawal certificate)
  ZC-->>C: proof(customer withdrawal)
  C-->>T: bundle(customer withdrawal cert + proof)
  T->>T: verify imported customer bundle
  T->>ZT: prove(teller approval certificate)
  ZT-->>T: proof(teller approval)
  T-->>C: bundle(teller approval cert + proof + nested chain)
  C->>C: import teller-certified approval and finish local derivation
  C->>ZC: prove(customer top-level authorization certificate)
  ZC-->>C: proof(customer top-level result)
  C-->>U: top cert + top proof + full chain
```

## Sequence: Four-Node Visa Authorization

This is the coalesced client-authority-payment-visa example.

```mermaid
sequenceDiagram
  participant U as Operator / etbctl
  participant C as Client node
  participant ZC as Client proof sidecar
  participant A as Authority node
  participant ZA as Authority proof sidecar
  participant P as Payment node
  participant ZP as Payment proof sidecar
  participant V as Visa node
  participant ZV as Visa proof sidecar

  U->>C: query trip_ready(alice)
  C->>C: Need authority says travel_authorized(alice)
  C->>A: remote query for authority says travel_authorized(alice)
  par Payment branch
    A->>P: remote query for payment says payment_cleared(alice)
    P->>P: Derive attested payment clearance
    P->>ZP: prove(payment certificate)
    ZP-->>P: payment proof
    P-->>A: payment bundle
  and Visa branch
    A->>V: remote query for visa says visa_approved(alice)
    V->>V: Derive attested visa approval
    V->>ZV: prove(visa certificate)
    ZV-->>V: visa proof
    V-->>A: visa bundle
  end
  A->>A: Verify both delegate bundles and finish authority rule
  A->>ZA: prove(authority certificate)
  ZA-->>A: authority proof
  A-->>C: authority bundle + nested payment/visa chain
  C->>C: Import authority-certified fact and derive trip_ready(alice)
  C->>ZC: prove(client top-level certificate)
  ZC-->>C: client proof
  C-->>U: top cert + top proof + 4-bundle chain
```

## Sequence: Independent Proof Verification

This shows the post-processing path after `--out-dir` or `--bundle-dir`
artifacts have been written to disk.

```mermaid
sequenceDiagram
  participant O as Operator
  participant Files as Saved .cert.cbor and .proof files
  participant V as zk-trace-check verify

  O->>Files: Choose cert/proof pair from output directory
  O->>V: verify cert.cbor proof
  V->>V: Read certificate bytes
  V->>V: Compute SHA-256(statement)
  V->>V: Decode commitment, announcement, response
  V->>V: Recompute Fiat-Shamir challenge
  V->>V: Check commitment-opening relation
  V-->>O: ok / reject
  Note over O,V: The current verifier proves a certificate-bound cryptographic relation.<br/>It does not yet prove full ETB derivation semantics.
```

## Reading The Diagrams

## Temporal Operators Today

The current fragment does include temporal syntax and partial temporal
enforcement, but it is important to be precise about what is and is not
attested yet.

- `A @ t` is parsed and stored in the AST, and evaluation checks it against the
  node's local wall clock.
- `A at T` is parsed and stored in the AST, but today it behaves as an exact
  annotation match against facts carrying the same `at T` tag.
- Temporal annotations survive canonicalization, so they are part of the atom
  text committed into trace digests, certificates, and the current proof input.
- The current verifier therefore proves a certificate-bound relation over atoms
  that include temporal annotations, but it does not independently validate the
  truth of wall-clock time or blockchain inclusion.

Concretely, the current implementation is:

- `@ t`: local clock enforcement in
  [eval.c](/Users/e35480/projects/misc/ETB/etb3/src/engine/eval.c)
- `at T`: exact-match temporal tagging in
  [eval.c](/Users/e35480/projects/misc/ETB/etb3/src/engine/eval.c)
- temporal parsing in
  [parser.c](/Users/e35480/projects/misc/ETB/etb3/src/core/parser.c)
- temporal canonicalization in
  [canon.c](/Users/e35480/projects/misc/ETB/etb3/src/core/canon.c)
- trace commitment of canonical atoms in
  [trace.c](/Users/e35480/projects/misc/ETB/etb3/src/engine/trace.c)

What is not implemented yet:

- signed time attestations
- blockchain inclusion proofs for `at T`
- an external consensus oracle for time/anchor truth
- a proof relation that validates temporal truth, rather than only committing to
  temporal annotations already present in the certificate

- Discovery is currently seed-based and best-effort.
- Route knowledge is carried by explicit announce/registry/resolve traffic.
- Imported remote bundles are verified before their answers are fed back into
  the local ETB engine.
- Each node emits its own certificate and proof; the top-level response returns
  a chain of these bundles so they can be checked independently.
- The current proof backend is real cryptography, but the proved statement is
  still narrower than the eventual full trace-check circuit.
