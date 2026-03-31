import fs from "node:fs";
import path from "node:path";
import http from "node:http";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "..");
const defaultBuildDir = path.join(rootDir, "build");
const defaultProverPath = path.join(
  rootDir,
  "adapters",
  "zk-trace-check",
  "target",
  "debug",
  "zk-trace-check"
);
const defaultControlDir = path.join(rootDir, ".etb", "ui-dashboard");
const publicDir = path.join(__dirname, "public");
const clients = new Set();
const managedNodes = new Map();
const seenDiscoveryRequests = new Set();

const nodePresets = [
  {
    id: "banking-customer",
    scenario: "Banking",
    label: "Customer",
    description: "Customer-side node that asks the teller to authorize a withdrawal.",
    nodeId: "customer",
    endpoint: "127.0.0.1:8601",
    programPath: path.join(rootDir, "examples", "live-banking", "customer_node.etb"),
    seeds: ["127.0.0.1:8602"],
    recommendedQuery: "cash_authorized(tx1001,alice,50)",
    queryRole: "primary"
  },
  {
    id: "banking-teller",
    scenario: "Banking",
    label: "Teller",
    description: "Bank teller node that issues the approval proof for the withdrawal.",
    nodeId: "teller",
    endpoint: "127.0.0.1:8602",
    programPath: path.join(rootDir, "examples", "live-banking", "teller_node.etb"),
    seeds: ["127.0.0.1:8601"],
    recommendedQuery: "teller says approved(tx1001,alice,50)",
    queryRole: "service"
  },
  {
    id: "visa-client",
    scenario: "Visa",
    label: "Client",
    description: "Client node that asks the authority to coalesce the travel proof chain.",
    nodeId: "client",
    endpoint: "127.0.0.1:8701",
    programPath: path.join(rootDir, "examples", "live-visa", "client.etb"),
    seeds: ["127.0.0.1:8702"],
    recommendedQuery: "trip_ready(alice)",
    queryRole: "primary"
  },
  {
    id: "visa-authority",
    scenario: "Visa",
    label: "Authority",
    description: "Authority node that coordinates payment and visa delegates.",
    nodeId: "authority",
    endpoint: "127.0.0.1:8702",
    programPath: path.join(rootDir, "examples", "live-visa", "authority.etb"),
    seeds: [],
    recommendedQuery: "authority says travel_authorized(alice)",
    queryRole: "service"
  },
  {
    id: "visa-payment",
    scenario: "Visa",
    label: "Payment",
    description: "Payment delegate that proves the fee was cleared.",
    nodeId: "payment",
    endpoint: "127.0.0.1:8703",
    programPath: path.join(rootDir, "examples", "live-visa", "payment.etb"),
    seeds: ["127.0.0.1:8702"],
    recommendedQuery: "payment says payment_cleared(alice)",
    queryRole: "service"
  },
  {
    id: "visa-visa",
    scenario: "Visa",
    label: "Visa Delegate",
    description: "Visa approval delegate that attests to the client's visa status.",
    nodeId: "visa",
    endpoint: "127.0.0.1:8704",
    programPath: path.join(rootDir, "examples", "live-visa", "visa.etb"),
    seeds: ["127.0.0.1:8702"],
    recommendedQuery: "visa says visa_approved(alice)",
    queryRole: "service"
  }
];

function parseArgs(argv) {
  const options = {
    host: "127.0.0.1",
    port: 4080,
    eventsFile: process.env.ETB_UI_EVENTS_FILE || "/tmp/etb-ui-events.jsonl",
    buildDir: defaultBuildDir,
    proverPath: defaultProverPath,
    controlDir: defaultControlDir,
    keepHistory: false
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if ((arg === "--port" || arg === "-p") && index + 1 < argv.length) {
      options.port = Number(argv[++index]);
    } else if (arg === "--host" && index + 1 < argv.length) {
      options.host = argv[++index];
    } else if (arg === "--events-file" && index + 1 < argv.length) {
      options.eventsFile = argv[++index];
    } else if (arg === "--build-dir" && index + 1 < argv.length) {
      options.buildDir = path.resolve(argv[++index]);
    } else if (arg === "--prover-path" && index + 1 < argv.length) {
      options.proverPath = path.resolve(argv[++index]);
    } else if (arg === "--control-dir" && index + 1 < argv.length) {
      options.controlDir = path.resolve(argv[++index]);
    } else if (arg === "--keep-history") {
      options.keepHistory = true;
    } else {
      throw new Error(
        `unknown option '${arg}'. usage: npm run ui -- --port PORT [--host HOST] [--events-file FILE] [--build-dir DIR] [--prover-path FILE] [--control-dir DIR] [--keep-history]`
      );
    }
  }
  if (!Number.isInteger(options.port) || options.port <= 0) {
    throw new Error(`invalid port '${options.port}'`);
  }
  return options;
}

const options = parseArgs(process.argv.slice(2));
const logsDir = path.join(options.controlDir, "logs");
const queriesDir = path.join(options.controlDir, "queries");
const etbdPath = path.join(options.buildDir, "etbd");
const etbctlPath = path.join(options.buildDir, "etbctl");

function createState(previous = null) {
  return {
    startedAt: previous ? previous.startedAt : Date.now(),
    nodes: new Map(),
    activity: previous ? previous.activity.slice() : [],
    messages: previous ? previous.messages.slice() : [],
    lastQuery: previous ? previous.lastQuery : null
  };
}

let state = createState();
let readOffset = 0;
let partialLine = "";

function discoveryKey(event) {
  return [
    event.op || "",
    event.fromNodeId || event.fromEndpoint || "external",
    event.toNodeId || event.toEndpoint || "unknown"
  ].join("|");
}

function isDiscoveryEvent(event) {
  return event && event.type === "request_received" && (event.op === "announce" || event.op === "registry");
}

function safeSegment(text) {
  return String(text || "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "") || "item";
}

function trimList(list, limit) {
  while (list.length > limit) {
    list.pop();
  }
}

function addActivity(entry) {
  state.activity.unshift(entry);
  trimList(state.activity, 120);
}

function addMessage(entry) {
  state.messages.unshift(entry);
  trimList(state.messages, 80);
}

function ensureNode(nodeId, extras = {}) {
  if (!nodeId) {
    return null;
  }
  let node = state.nodes.get(nodeId);
  if (!node) {
    node = {
      id: nodeId,
      endpoint: "",
      programPath: "",
      pid: null,
      status: "external",
      kind: "external",
      busy: false,
      currentQuery: "",
      lastResult: null,
      lastSeenAt: Date.now(),
      startedAt: null
    };
    state.nodes.set(nodeId, node);
  }
  Object.assign(node, extras);
  return node;
}

function removeNode(nodeId) {
  if (!nodeId) {
    return;
  }
  state.nodes.delete(nodeId);
}

function presetSummary(preset) {
  const managed = managedNodes.get(preset.id);
  const node = state.nodes.get(preset.nodeId);
  const running = !!(
    managed &&
    managed.child &&
    managed.child.exitCode == null &&
    managed.stopping !== true
  );
  return {
    id: preset.id,
    scenario: preset.scenario,
    label: preset.label,
    description: preset.description,
    nodeId: preset.nodeId,
    endpoint: preset.endpoint,
    programPath: preset.programPath,
    seeds: preset.seeds.slice(),
    recommendedQuery: preset.recommendedQuery,
    queryRole: preset.queryRole,
    running,
    logPath: managed ? managed.logPath : "",
    pid: running
      ? managed.child.pid
      : node && typeof node.pid === "number"
        ? node.pid
        : null
  };
}

function serializeState() {
  return {
    startedAt: state.startedAt,
    nodes: Array.from(state.nodes.values()).sort((lhs, rhs) =>
      lhs.id.localeCompare(rhs.id)
    ),
    activity: state.activity.slice(),
    messages: state.messages.slice(),
    eventsFile: options.eventsFile,
    catalog: nodePresets.map(presetSummary),
    lastQuery: state.lastQuery,
    controlDir: options.controlDir
  };
}

function broadcast(event) {
  const payload = `data: ${JSON.stringify(event)}\n\n`;
  for (const client of clients) {
    client.write(payload);
  }
}

function broadcastSnapshot() {
  broadcast({ type: "snapshot", state: serializeState() });
}

function ensureManagedNodesVisible() {
  const now = Date.now();
  for (const preset of nodePresets) {
    const managed = managedNodes.get(preset.id);
    if (!managed) {
      continue;
    }
    const child = managed.child;
    const status =
      managed.stopping === true
        ? "dead"
        : child && child.exitCode == null
          ? "starting"
          : "dead";
    ensureNode(preset.nodeId, {
      endpoint: preset.endpoint,
      programPath: preset.programPath,
      pid: child && typeof child.pid === "number" ? child.pid : null,
      status,
      kind: "node",
      lastSeenAt: now,
      startedAt: managed.startedAt || now
    });
  }
}

async function ensureControlDirectories() {
  await fs.promises.mkdir(path.dirname(options.eventsFile), { recursive: true });
  await fs.promises.mkdir(logsDir, { recursive: true });
  await fs.promises.mkdir(queriesDir, { recursive: true });
}

async function ensureEventsFile() {
  await ensureControlDirectories();
  if (options.keepHistory) {
    await fs.promises.appendFile(options.eventsFile, "", "utf8");
    const content = await fs.promises.readFile(options.eventsFile, "utf8");
    readOffset = Buffer.byteLength(content);
    applyEventLines(content);
    ensureManagedNodesVisible();
  } else {
    await fs.promises.writeFile(options.eventsFile, "", "utf8");
    state = createState();
    readOffset = 0;
    partialLine = "";
    seenDiscoveryRequests.clear();
  }
}

function applyEvent(event) {
  if (!event || typeof event !== "object") {
    return false;
  }
  const ts = typeof event.ts === "number" ? event.ts : Date.now();
  switch (event.type) {
    case "node_started": {
      ensureNode(event.nodeId, {
        endpoint: event.endpoint || "",
        programPath: event.programPath || "",
        pid: typeof event.pid === "number" ? event.pid : null,
        status: "alive",
        kind: "node",
        startedAt: ts,
        lastSeenAt: ts
      });
      addActivity({
        id: `${ts}:node:${event.nodeId}`,
        ts,
        kind: "node_started",
        title: `${event.nodeId} is listening`,
        detail: event.endpoint || ""
      });
      return true;
    }
    case "request_received": {
      if (isDiscoveryEvent(event)) {
        const key = discoveryKey(event);
        if (seenDiscoveryRequests.has(key)) {
          return false;
        }
        seenDiscoveryRequests.add(key);
      }
      const destination = ensureNode(event.toNodeId, {
        endpoint: event.toEndpoint || "",
        status: "alive",
        kind: "node",
        lastSeenAt: ts
      });
      const source = ensureNode(event.fromNodeId || "external", {
        endpoint: event.fromEndpoint || "",
        status: event.fromEndpoint ? "alive" : "external",
        kind: event.fromEndpoint ? "node" : "external",
        lastSeenAt: ts
      });
      if (source && event.fromEndpoint && !source.endpoint) {
        source.endpoint = event.fromEndpoint;
      }
      addMessage({
        id: `${ts}:msg:${event.toNodeId}:${event.op || "rpc"}`,
        ts,
        fromNodeId: source ? source.id : "",
        toNodeId: destination ? destination.id : "",
        op: event.op || "rpc",
        query: event.query || "",
        principal: event.principal || ""
      });
      addActivity({
        id: `${ts}:req:${event.toNodeId}:${event.op || "rpc"}`,
        ts,
        kind: event.op === "query" ? "request_query" : "request_received",
        title: `${source ? source.id : "unknown"} -> ${destination ? destination.id : "unknown"}`,
        detail: event.query || event.principal || event.op || "rpc"
      });
      return true;
    }
    case "query_started": {
      const node = ensureNode(event.nodeId, {
        status: "alive",
        kind: "node",
        busy: true,
        currentQuery: event.query || "",
        lastSeenAt: ts
      });
      addActivity({
        id: `${ts}:query-start:${event.nodeId}`,
        ts,
        kind: "query_started",
        title: `${node ? node.id : event.nodeId} started query`,
        detail: event.query || ""
      });
      return true;
    }
    case "query_finished": {
      const node = ensureNode(event.nodeId, {
        status: event.success ? "alive" : "error",
        kind: "node",
        busy: false,
        currentQuery: "",
        lastSeenAt: ts,
        lastResult: {
          success: !!event.success,
          answerCount: event.answerCount || 0,
          bundleCount: event.bundleCount || 0,
          error: event.error || ""
        }
      });
      addActivity({
        id: `${ts}:query-finish:${event.nodeId}`,
        ts,
        kind: "query_finished",
        title: `${node ? node.id : event.nodeId} ${event.success ? "completed" : "failed"}`,
        detail: event.success
          ? `${event.answerCount || 0} answers, ${event.bundleCount || 0} bundles`
          : event.error || "query failed"
      });
      return true;
    }
    case "bundle_imported": {
      ensureNode(event.nodeId, {
        status: "alive",
        kind: "node",
        lastSeenAt: ts
      });
      ensureNode(event.sourceNodeId, {
        status: "alive",
        kind: event.sourceNodeId ? "node" : "external",
        lastSeenAt: ts
      });
      addActivity({
        id: `${ts}:bundle:${event.nodeId}`,
        ts,
        kind: "bundle_imported",
        title: `${event.nodeId} imported a bundle`,
        detail: event.sourceNodeId
          ? `from ${event.sourceNodeId}${event.query ? ` for ${event.query}` : ""}`
          : event.query || ""
      });
      return true;
    }
    case "logic_invoke": {
      ensureNode(event.nodeId, {
        status: "alive",
        kind: "node",
        lastSeenAt: ts
      });
      addActivity({
        id: `${ts}:logic:${event.nodeId}:${event.targetPrincipal || "remote"}`,
        ts,
        kind: "logic_invoke",
        title: `${event.nodeId} invoked a remote Datalog goal`,
        detail:
          event.goal ||
          (event.targetPrincipal
            ? `${event.targetPrincipal}${event.targetEndpoint ? ` @ ${event.targetEndpoint}` : ""}`
            : event.targetEndpoint || "remote invocation")
      });
      return true;
    }
    case "node_status": {
      ensureNode(event.nodeId, {
        status: event.status || "unknown",
        lastSeenAt: ts
      });
      return true;
    }
    default:
      return false;
  }
}

function applyEventLines(text) {
  if (!text) {
    return;
  }
  const lines = text.split("\n");
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    try {
      applyEvent(JSON.parse(trimmed));
    } catch {
      console.warn(`failed to parse telemetry line: ${trimmed}`);
    }
  }
}

async function readNewEvents() {
  let stat;
  try {
    stat = await fs.promises.stat(options.eventsFile);
  } catch {
    return;
  }
  if (stat.size < readOffset) {
    const previous = state;
    readOffset = 0;
    partialLine = "";
    state = createState(previous);
    seenDiscoveryRequests.clear();
    ensureManagedNodesVisible();
    broadcast({ type: "snapshot_reset", state: serializeState() });
  }
  if (stat.size === readOffset) {
    return;
  }
  const length = stat.size - readOffset;
  const handle = await fs.promises.open(options.eventsFile, "r");
  const buffer = Buffer.alloc(length);
  await handle.read(buffer, 0, length, readOffset);
  await handle.close();
  readOffset = stat.size;
  partialLine += buffer.toString("utf8");
  const lines = partialLine.split("\n");
  partialLine = lines.pop() || "";
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    try {
      const event = JSON.parse(trimmed);
      if (applyEvent(event)) {
        broadcast(event);
      }
    } catch {
      console.warn(`failed to parse telemetry line: ${trimmed}`);
    }
  }
}

function checkNodeProcesses() {
  const now = Date.now();
  for (const node of state.nodes.values()) {
    if (!node.pid || node.kind !== "node") {
      continue;
    }
    let nextStatus = "alive";
    try {
      process.kill(node.pid, 0);
    } catch {
      nextStatus = "dead";
    }
    if (node.busy && nextStatus === "alive") {
      nextStatus = "busy";
    }
    if (node.status !== nextStatus) {
      node.status = nextStatus;
      node.lastSeenAt = now;
      broadcast({
        type: "node_status",
        ts: now,
        nodeId: node.id,
        status: nextStatus
      });
    }
  }
}

async function readJsonBody(request) {
  const chunks = [];
  for await (const chunk of request) {
    chunks.push(chunk);
  }
  const text = Buffer.concat(chunks).toString("utf8").trim();
  if (!text) {
    return {};
  }
  return JSON.parse(text);
}

function sendJson(response, statusCode, body) {
  response.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store"
  });
  response.end(JSON.stringify(body));
}

function sendFile(response, filePath, contentType) {
  const stream = fs.createReadStream(filePath);
  stream.on("error", () => {
    response.writeHead(404);
    response.end("not found");
  });
  response.writeHead(200, {
    "Content-Type": contentType,
    "Cache-Control": "no-store"
  });
  stream.pipe(response);
}

function vendorPath(name) {
  switch (name) {
    case "react.js":
      return path.join(rootDir, "node_modules", "react", "umd", "react.development.js");
    case "react-dom.js":
      return path.join(rootDir, "node_modules", "react-dom", "umd", "react-dom.development.js");
    default:
      return null;
  }
}

function findPreset(presetId) {
  return nodePresets.find((preset) => preset.id === presetId) || null;
}

function parseQueryOutput(stdout) {
  const result = {
    root: "",
    answers: [],
    bundles: 0
  };
  const lines = stdout.split(/\r?\n/);
  for (const line of lines) {
    if (line.startsWith("root=")) {
      result.root = line.slice("root=".length);
    } else if (line.startsWith("answer ")) {
      const parts = line.split(":");
      result.answers.push(parts.slice(1).join(":").trim());
    } else if (line.startsWith("bundles=")) {
      result.bundles = Number(line.slice("bundles=".length)) || 0;
    }
  }
  return result;
}

function spawnCommand(command, args, extra = {}) {
  return new Promise((resolve) => {
    const child = spawn(command, args, {
      cwd: rootDir,
      env: extra.env || process.env,
      stdio: ["ignore", "pipe", "pipe"]
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString("utf8");
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString("utf8");
    });
    child.on("close", (code, signal) => {
      resolve({ code, signal, stdout, stderr });
    });
  });
}

async function startPresetNode(presetId) {
  const preset = findPreset(presetId);
  const now = Date.now();
  if (preset == null) {
    throw new Error(`unknown preset '${presetId}'`);
  }
  {
    const existingNode = state.nodes.get(preset.nodeId);
    if (
      existingNode &&
      existingNode.kind === "node" &&
      existingNode.endpoint === preset.endpoint &&
      existingNode.status !== "dead"
    ) {
      return {
        ok: true,
        message: `${preset.nodeId} is already visible at ${preset.endpoint}`
      };
    }
  }
  const existing = managedNodes.get(preset.id);
  if (existing && existing.child && existing.child.exitCode == null) {
    return {
      ok: true,
      message: `${preset.nodeId} is already running`
    };
  }
  await fs.promises.access(etbdPath, fs.constants.X_OK);
  await fs.promises.access(options.proverPath, fs.constants.X_OK);
  await ensureControlDirectories();

  const logPath = path.join(logsDir, `${preset.id}.log`);
  const logStream = fs.createWriteStream(logPath, { flags: "w" });
  const args = [
    "serve",
    preset.programPath,
    "--node-id",
    preset.nodeId,
    "--listen",
    preset.endpoint
  ];
  for (const seed of preset.seeds) {
    args.push("--seed", seed);
  }
  args.push("--prover", options.proverPath);
  const child = spawn(etbdPath, args, {
    cwd: rootDir,
    env: {
      ...process.env,
      ETB_UI_EVENTS_FILE: options.eventsFile
    },
    stdio: ["ignore", "pipe", "pipe"]
  });
  child.stdout.pipe(logStream);
  child.stderr.pipe(logStream);

  managedNodes.set(preset.id, {
    preset,
    child,
    logPath,
    logStream,
    startedAt: now,
    stopping: false
  });

  ensureNode(preset.nodeId, {
    endpoint: preset.endpoint,
    programPath: preset.programPath,
    pid: child.pid || null,
    status: "starting",
    kind: "node",
    lastSeenAt: now,
    startedAt: now
  });
  addActivity({
    id: `${now}:launch:${preset.nodeId}`,
    ts: now,
    kind: "control_start",
    title: `Launching ${preset.nodeId}`,
    detail: `${preset.endpoint} from ${preset.scenario}`
  });

  child.on("exit", (code, signal) => {
    const managed = managedNodes.get(preset.id);
    const ts = Date.now();
    if (managed && managed.logStream) {
      managed.logStream.end();
    }
    if (managed && managed.stopping) {
      removeNode(preset.nodeId);
      managedNodes.delete(preset.id);
    } else {
      ensureNode(preset.nodeId, {
        status: "dead",
        busy: false,
        currentQuery: "",
        lastSeenAt: ts
      });
      if (managed) {
        managedNodes.delete(preset.id);
      }
    }
    addActivity({
      id: `${ts}:exit:${preset.nodeId}`,
      ts,
      kind: "node_exit",
      title: `${preset.nodeId} stopped`,
      detail:
        managed && managed.stopping
          ? "stopped by dashboard"
          : `exit=${code == null ? "?" : code}${signal ? ` signal=${signal}` : ""}`
    });
    if (managed) {
      managed.stopping = false;
    }
    broadcastSnapshot();
  });

  broadcastSnapshot();
  return {
    ok: true,
    message: `launch requested for ${preset.nodeId}`,
    logPath
  };
}

async function stopPresetNode(presetId) {
  const preset = findPreset(presetId);
  const now = Date.now();
  if (preset == null) {
    throw new Error(`unknown preset '${presetId}'`);
  }
  const managed = managedNodes.get(preset.id);
  if (!managed || !managed.child || managed.child.exitCode != null) {
    removeNode(preset.nodeId);
    managedNodes.delete(preset.id);
    return {
      ok: true,
      message: `${preset.nodeId} is not running`
    };
  }
  managed.stopping = true;
  managed.child.kill("SIGTERM");
  addActivity({
    id: `${now}:stop:${preset.nodeId}`,
    ts: now,
    kind: "control_stop",
    title: `Stopping ${preset.nodeId}`,
    detail: preset.endpoint
  });
  broadcastSnapshot();
  return {
    ok: true,
    message: `stop requested for ${preset.nodeId}`
  };
}

function selectQueryNode(nodeId) {
  const node = state.nodes.get(nodeId);
  if (
    !node ||
    node.kind !== "node" ||
    !node.endpoint ||
    node.status === "dead"
  ) {
    throw new Error(`node '${nodeId}' is not available for queries`);
  }
  return node;
}

async function runQuery(nodeId, queryText) {
  const node = selectQueryNode(nodeId);
  const stamp = `${Date.now()}-${safeSegment(nodeId)}`;
  const outputDir = path.join(queriesDir, stamp);
  const certificatePath = path.join(outputDir, "top.cert.cbor");
  const proofPath = path.join(outputDir, "top.proof");
  const bundleDir = path.join(outputDir, "chain");
  const stdoutPath = path.join(outputDir, "query.stdout.txt");
  const stderrPath = path.join(outputDir, "query.stderr.txt");
  const now = Date.now();

  await fs.promises.access(etbctlPath, fs.constants.X_OK);
  await fs.promises.access(options.proverPath, fs.constants.X_OK);
  await fs.promises.mkdir(outputDir, { recursive: true });

  addActivity({
    id: `${now}:ui-query:${node.id}`,
    ts: now,
    kind: "control_query",
    title: `Dashboard query -> ${node.id}`,
    detail: queryText
  });
  broadcastSnapshot();

  const result = await spawnCommand(
    etbctlPath,
    [
      "query",
      node.endpoint,
      queryText,
      "--cert-out",
      certificatePath,
      "--proof-out",
      proofPath,
      "--bundle-dir",
      bundleDir,
      "--prover",
      options.proverPath
    ],
    {
      env: {
        ...process.env,
        ETB_UI_EVENTS_FILE: options.eventsFile
      }
    }
  );

  await fs.promises.writeFile(stdoutPath, result.stdout, "utf8");
  await fs.promises.writeFile(stderrPath, result.stderr, "utf8");

  if (result.code !== 0) {
    state.lastQuery = {
      id: stamp,
      ts: Date.now(),
      nodeId: node.id,
      endpoint: node.endpoint,
      queryText,
      outputDir,
      certificatePath: "",
      proofPath: "",
      bundleDir,
      stdoutPath,
      stderrPath,
      stdout: result.stdout,
      stderr: result.stderr,
      success: false,
      verification: null
    };
    addActivity({
      id: `${Date.now()}:ui-query-failed:${node.id}`,
      ts: Date.now(),
      kind: "control_query_failed",
      title: `Dashboard query failed on ${node.id}`,
      detail: result.stderr.trim() || "query command failed"
    });
    broadcastSnapshot();
    throw new Error(result.stderr.trim() || "query command failed");
  }

  const parsed = parseQueryOutput(result.stdout);
  state.lastQuery = {
    id: stamp,
    ts: Date.now(),
    nodeId: node.id,
    endpoint: node.endpoint,
    queryText,
    outputDir,
    certificatePath,
    proofPath,
    bundleDir,
    stdoutPath,
    stderrPath,
    stdout: result.stdout,
    stderr: result.stderr,
    success: true,
    root: parsed.root,
    answers: parsed.answers,
    bundles: parsed.bundles,
    verification: null
  };
  addActivity({
    id: `${Date.now()}:ui-query-ok:${node.id}`,
    ts: Date.now(),
    kind: "control_query_ok",
    title: `Dashboard query completed on ${node.id}`,
    detail: `${parsed.answers.length} answers, ${parsed.bundles} bundles`
  });
  broadcastSnapshot();
  return state.lastQuery;
}

async function collectVerificationTargets(lastQuery) {
  const targets = [];
  if (lastQuery.certificatePath && lastQuery.proofPath) {
    targets.push({
      label: "top-level",
      certificatePath: lastQuery.certificatePath,
      proofPath: lastQuery.proofPath
    });
  }
  if (!lastQuery.bundleDir) {
    return targets;
  }
  let entries = [];
  try {
    entries = await fs.promises.readdir(lastQuery.bundleDir);
  } catch {
    return targets;
  }
  for (const entry of entries.sort()) {
    if (!entry.endsWith(".proof")) {
      continue;
    }
    const proofPath = path.join(lastQuery.bundleDir, entry);
    const certificatePath = path.join(
      lastQuery.bundleDir,
      entry.slice(0, -".proof".length) + ".cert.cbor"
    );
    try {
      await fs.promises.access(certificatePath, fs.constants.R_OK);
      targets.push({
        label: entry,
        certificatePath,
        proofPath
      });
    } catch {
      continue;
    }
  }
  return targets;
}

async function verifyLastQuery() {
  if (!state.lastQuery || !state.lastQuery.success) {
    throw new Error("no successful dashboard query is available to verify");
  }
  await fs.promises.access(options.proverPath, fs.constants.X_OK);
  const targets = await collectVerificationTargets(state.lastQuery);
  if (targets.length === 0) {
    throw new Error("no proof artifacts were found for the last query");
  }
  const results = [];
  for (const target of targets) {
    const result = await spawnCommand(options.proverPath, [
      "verify",
      target.certificatePath,
      target.proofPath
    ]);
    results.push({
      label: target.label,
      certificatePath: target.certificatePath,
      proofPath: target.proofPath,
      success: result.code === 0,
      stdout: result.stdout,
      stderr: result.stderr
    });
    if (result.code !== 0) {
      break;
    }
  }
  state.lastQuery = {
    ...state.lastQuery,
    verification: {
      ts: Date.now(),
      success: results.every((entry) => entry.success),
      results
    }
  };
  addActivity({
    id: `${Date.now()}:verify:${state.lastQuery.nodeId}`,
    ts: Date.now(),
    kind: "control_verify",
    title: state.lastQuery.verification.success
      ? "Proof verification passed"
      : "Proof verification failed",
    detail: `${results.filter((entry) => entry.success).length}/${results.length} artifacts verified`
  });
  broadcastSnapshot();
  return state.lastQuery.verification;
}

async function handleApiRequest(request, response, url) {
  try {
    if (request.method === "GET" && url.pathname === "/api/state") {
      sendJson(response, 200, serializeState());
      return true;
    }
    if (request.method === "GET" && url.pathname === "/api/events") {
      response.writeHead(200, {
        "Content-Type": "text/event-stream; charset=utf-8",
        "Cache-Control": "no-cache, no-transform",
        Connection: "keep-alive"
      });
      response.write(`data: ${JSON.stringify({ type: "snapshot", state: serializeState() })}\n\n`);
      clients.add(response);
      request.on("close", () => {
        clients.delete(response);
      });
      return true;
    }
    if (request.method === "POST" && url.pathname === "/api/nodes/start") {
      const body = await readJsonBody(request);
      const result = await startPresetNode(body.presetId);
      sendJson(response, 200, {
        ok: true,
        message: result.message,
        state: serializeState()
      });
      return true;
    }
    if (request.method === "POST" && url.pathname === "/api/nodes/stop") {
      const body = await readJsonBody(request);
      const result = await stopPresetNode(body.presetId);
      sendJson(response, 200, {
        ok: true,
        message: result.message,
        state: serializeState()
      });
      return true;
    }
    if (request.method === "POST" && url.pathname === "/api/query") {
      const body = await readJsonBody(request);
      if (typeof body.queryText !== "string" || body.queryText.trim() === "") {
        throw new Error("queryText is required");
      }
      const result = await runQuery(body.nodeId, body.queryText.trim());
      sendJson(response, 200, {
        ok: true,
        lastQuery: result,
        state: serializeState()
      });
      return true;
    }
    if (request.method === "POST" && url.pathname === "/api/query/verify") {
      const verification = await verifyLastQuery();
      sendJson(response, 200, {
        ok: true,
        verification,
        state: serializeState()
      });
      return true;
    }
  } catch (error) {
    sendJson(response, 400, {
      ok: false,
      error: error instanceof Error ? error.message : String(error),
      state: serializeState()
    });
    return true;
  }
  return false;
}

async function serveRequest(request, response) {
  const url = new URL(request.url || "/", `http://${options.host}:${options.port}`);
  if (await handleApiRequest(request, response, url)) {
    return;
  }
  if (url.pathname === "/vendor/react.js" || url.pathname === "/vendor/react-dom.js") {
    const vendor = vendorPath(path.basename(url.pathname));
    if (vendor == null) {
      response.writeHead(404);
      response.end("not found");
      return;
    }
    sendFile(response, vendor, "application/javascript; charset=utf-8");
    return;
  }
  if (url.pathname === "/app.js") {
    sendFile(response, path.join(publicDir, "app.js"), "application/javascript; charset=utf-8");
    return;
  }
  if (url.pathname === "/styles.css") {
    sendFile(response, path.join(publicDir, "styles.css"), "text/css; charset=utf-8");
    return;
  }
  sendFile(response, path.join(publicDir, "index.html"), "text/html; charset=utf-8");
}

function shutdownManagedNodes() {
  for (const managed of managedNodes.values()) {
    if (managed.child && managed.child.exitCode == null) {
      try {
        managed.child.kill("SIGTERM");
      } catch {
        continue;
      }
    }
  }
}

process.on("SIGINT", () => {
  shutdownManagedNodes();
  process.exit(0);
});

process.on("SIGTERM", () => {
  shutdownManagedNodes();
  process.exit(0);
});

await ensureEventsFile();

setInterval(() => {
  readNewEvents().catch((error) => {
    console.error(error);
  });
}, 250);

setInterval(() => {
  checkNodeProcesses();
}, 1000);

const server = http.createServer((request, response) => {
  serveRequest(request, response).catch((error) => {
    console.error(error);
    sendJson(response, 500, {
      ok: false,
      error: error instanceof Error ? error.message : String(error)
    });
  });
});

server.listen(options.port, options.host, () => {
  console.log(
    `etb-ui listening on http://${options.host}:${options.port} (events: ${options.eventsFile})`
  );
});
