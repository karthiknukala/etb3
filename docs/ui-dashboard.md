# React Dashboard

The repo now includes a local React dashboard for live ETB nodes and a small
interactive control deck.

## What It Shows

- nodes as they start listening
- query start/finish state on each node
- discovery and query traffic between nodes
- bundle import activity
- node liveness based on local process status
- a launch catalog for the banking and visa example nodes
- a small command line pane for sending live ETB queries
- proof verification status for the most recent dashboard query

Communication events are drawn as animated dots travelling between nodes in the
browser.

## Start The UI

Install the UI dependencies once:

```sh
npm install
```

Start the dashboard on localhost:

```sh
npm run ui -- --port 4090
```

Optional flags:

- `--host HOST`
- `--events-file PATH`
- `--keep-history`

By default the server binds to `127.0.0.1`, listens on the port you provide,
and reads telemetry from `/tmp/etb-ui-events.jsonl`.

The dashboard also writes its own control-plane artifacts under:

- `.etb/ui-dashboard/logs/`
- `.etb/ui-dashboard/queries/`

Each UI-submitted query gets its own output directory with:

- `top.cert.cbor`
- `top.proof`
- `chain/`
- `query.stdout.txt`
- `query.stderr.txt`

## Recommended Workflow

1. Start the dashboard first.
2. Open the `Nodes` tab in the dashboard and start the example nodes you want.
3. Wait for them to appear as live in the graph.
4. Open the `Query` tab, target a live node, and submit a query.
5. Use `Run Proof Checker` to verify the most recent returned artifacts.
6. Watch the graph update in real time.

Example:

```sh
npm run ui -- --port 4090
```

```sh
cmake --build build -j4
```

Then, from the dashboard:

1. Start `Customer` and `Teller` from the `Nodes` tab.
2. Query `cash_authorized(tx1001,alice,50)` against `customer`.
3. Run the proof checker button.

For the visa scenario:

1. Start `Authority`, `Payment`, `Visa Delegate`, and `Client`.
2. Query `trip_ready(alice)` against `client`.
3. Run the proof checker button.

The old demo scripts still work, but the dashboard can now drive these flows
directly.

## Telemetry Model

The current dashboard is fed by JSONL telemetry emitted by live ETB nodes. The
important event classes are:

- `node_started`
- `request_received`
- `query_started`
- `query_finished`
- `bundle_imported`

The dashboard server watches the event file, exposes `/api/state`, and streams
live updates over `/api/events` using server-sent events.

The control deck talks to additional local endpoints exposed by the UI server:

- `POST /api/nodes/start`
- `POST /api/nodes/stop`
- `POST /api/query`
- `POST /api/query/verify`

## Current Limits

- Start the UI before the nodes if you want a clean session without old events.
- This is local-machine observability, not a secured multi-user admin console.
- Discovery traffic is seed-based and best-effort, so the graph reflects the
  current prototype rather than the future signed-heartbeat design.
- The dashboard verifier checks the repo's current proof relation and saved
  proof artifacts; it is not yet a full ETB trace-semantics verifier.
