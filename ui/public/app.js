(function () {
  const h = React.createElement;
  const useEffect = React.useEffect;
  const useMemo = React.useMemo;
  const useState = React.useState;

  function formatTime(ts) {
    return new Date(ts).toLocaleTimeString();
  }

  function statusTone(node) {
    if (node.status === "dead") {
      return "dead";
    }
    if (node.status === "error" || (node.lastResult && node.lastResult.success === false)) {
      return "error";
    }
    if (node.status === "starting" || node.busy || node.status === "busy") {
      return "busy";
    }
    if (node.kind === "external") {
      return "external";
    }
    return "alive";
  }

  function activityTone(entry) {
    if (!entry) {
      return "neutral";
    }
    if (
      entry.kind === "logic_invoke" ||
      entry.kind === "query_started" ||
      entry.kind === "query_finished" ||
      entry.kind === "bundle_imported" ||
      entry.kind === "request_query" ||
      entry.kind === "control_query" ||
      entry.kind === "control_query_ok" ||
      entry.kind === "control_query_failed" ||
      entry.kind === "control_verify"
    ) {
      return "logic";
    }
    if (entry.kind === "node_exit") {
      return "error";
    }
    if (entry.kind === "request_received") {
      return "transport";
    }
    return "neutral";
  }

  function normalizeSnapshot(snapshot) {
    return {
      startedAt: snapshot.startedAt || Date.now(),
      eventsFile: snapshot.eventsFile || "",
      controlDir: snapshot.controlDir || "",
      nodes: Array.isArray(snapshot.nodes) ? snapshot.nodes : [],
      activity: Array.isArray(snapshot.activity) ? snapshot.activity : [],
      messages: Array.isArray(snapshot.messages) ? snapshot.messages : [],
      catalog: Array.isArray(snapshot.catalog) ? snapshot.catalog : [],
      lastQuery: snapshot.lastQuery || null
    };
  }

  function upsertNode(nodes, patch) {
    const next = nodes.slice();
    const index = next.findIndex((node) => node.id === patch.id);
    if (index >= 0) {
      next[index] = Object.assign({}, next[index], patch);
    } else {
      next.push(
        Object.assign(
          {
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
          },
          patch
        )
      );
    }
    next.sort((lhs, rhs) => lhs.id.localeCompare(rhs.id));
    return next;
  }

  function pushFront(list, entry, limit) {
    const next = [entry].concat(list);
    return next.slice(0, limit);
  }

  function applyDashboardEvent(snapshot, event) {
    if (!event || typeof event !== "object") {
      return snapshot;
    }
    if ((event.type === "snapshot" || event.type === "snapshot_reset") && event.state) {
      return normalizeSnapshot(event.state);
    }

    let next = Object.assign({}, snapshot);
    next.nodes = snapshot.nodes.slice();
    next.activity = snapshot.activity.slice();
    next.messages = snapshot.messages.slice();
    next.catalog = snapshot.catalog.slice();
    next.lastQuery = snapshot.lastQuery;

    if (event.type === "node_started") {
      next.nodes = upsertNode(next.nodes, {
        id: event.nodeId,
        endpoint: event.endpoint || "",
        programPath: event.programPath || "",
        pid: event.pid || null,
        status: "alive",
        kind: "node",
        startedAt: event.ts,
        lastSeenAt: event.ts
      });
      next.activity = pushFront(
        next.activity,
        {
          id: `${event.ts}:node:${event.nodeId}`,
          ts: event.ts,
          kind: "node_started",
          title: `${event.nodeId} is listening`,
          detail: event.endpoint || ""
        },
        120
      );
      return next;
    }

    if (event.type === "request_received") {
      if (event.fromNodeId) {
        next.nodes = upsertNode(next.nodes, {
          id: event.fromNodeId,
          endpoint: event.fromEndpoint || "",
          kind: event.fromEndpoint ? "node" : "external",
          status: event.fromEndpoint ? "alive" : "external",
          lastSeenAt: event.ts
        });
      }
      if (event.toNodeId) {
        next.nodes = upsertNode(next.nodes, {
          id: event.toNodeId,
          endpoint: event.toEndpoint || "",
          kind: "node",
          status: "alive",
          lastSeenAt: event.ts
        });
      }
      next.messages = pushFront(
        next.messages,
        {
          id: `${event.ts}:msg:${event.toNodeId}:${event.op || "rpc"}`,
          ts: event.ts,
          fromNodeId: event.fromNodeId || "",
          toNodeId: event.toNodeId || "",
          op: event.op || "rpc",
          query: event.query || "",
          principal: event.principal || ""
        },
        80
      );
      next.activity = pushFront(
        next.activity,
        {
          id: `${event.ts}:req:${event.toNodeId}`,
          ts: event.ts,
          kind: event.op === "query" ? "request_query" : "request_received",
          title: `${event.fromNodeId || "external"} -> ${event.toNodeId || "node"}`,
          detail: event.query || event.principal || event.op || "rpc"
        },
        120
      );
      return next;
    }

    if (event.type === "query_started") {
      next.nodes = upsertNode(next.nodes, {
        id: event.nodeId,
        status: "busy",
        kind: "node",
        busy: true,
        currentQuery: event.query || "",
        lastSeenAt: event.ts
      });
      next.activity = pushFront(
        next.activity,
        {
          id: `${event.ts}:query-start:${event.nodeId}`,
          ts: event.ts,
          kind: "query_started",
          title: `${event.nodeId} started query`,
          detail: event.query || ""
        },
        120
      );
      return next;
    }

    if (event.type === "query_finished") {
      next.nodes = upsertNode(next.nodes, {
        id: event.nodeId,
        status: event.success ? "alive" : "error",
        kind: "node",
        busy: false,
        currentQuery: "",
        lastSeenAt: event.ts,
        lastResult: {
          success: !!event.success,
          answerCount: event.answerCount || 0,
          bundleCount: event.bundleCount || 0,
          error: event.error || ""
        }
      });
      next.activity = pushFront(
        next.activity,
        {
          id: `${event.ts}:query-finish:${event.nodeId}`,
          ts: event.ts,
          kind: "query_finished",
          title: `${event.nodeId} ${event.success ? "completed" : "failed"}`,
          detail: event.success
            ? `${event.answerCount || 0} answers, ${event.bundleCount || 0} bundles`
            : event.error || "query failed"
        },
        120
      );
      return next;
    }

    if (event.type === "bundle_imported") {
      next.activity = pushFront(
        next.activity,
        {
          id: `${event.ts}:bundle:${event.nodeId}`,
          ts: event.ts,
          kind: "bundle_imported",
          title: `${event.nodeId} imported a bundle`,
          detail: event.sourceNodeId
            ? `from ${event.sourceNodeId}${event.query ? ` for ${event.query}` : ""}`
            : event.query || ""
        },
        120
      );
      return next;
    }

    if (event.type === "logic_invoke") {
      next.activity = pushFront(
        next.activity,
        {
          id: `${event.ts}:logic:${event.nodeId}:${event.targetPrincipal || "remote"}`,
          ts: event.ts,
          kind: "logic_invoke",
          title: `${event.nodeId} invoked a remote Datalog goal`,
          detail:
            event.goal ||
            (event.targetPrincipal
              ? `${event.targetPrincipal}${event.targetEndpoint ? ` @ ${event.targetEndpoint}` : ""}`
              : event.targetEndpoint || "remote invocation")
        },
        120
      );
      return next;
    }

    if (event.type === "node_status") {
      next.nodes = upsertNode(next.nodes, {
        id: event.nodeId,
        status: event.status || "unknown",
        lastSeenAt: event.ts
      });
      return next;
    }

    return snapshot;
  }

  function graphLayout(nodes, width, height) {
    const ordered = nodes.slice().sort((lhs, rhs) => lhs.id.localeCompare(rhs.id));
    const regular = ordered.filter((node) => node.kind !== "external");
    const external = ordered.filter((node) => node.kind === "external");
    const positions = {};

    regular.forEach((node, index) => {
      const angle = -Math.PI / 2 + (index * (Math.PI * 2)) / Math.max(regular.length, 1);
      const radius = Math.min(width, height) * 0.32;
      positions[node.id] = {
        x: width / 2 + Math.cos(angle) * radius,
        y: height / 2 + Math.sin(angle) * radius
      };
    });

    external.forEach((node, index) => {
      positions[node.id] = {
        x: 110,
        y: 120 + index * 104
      };
    });

    return positions;
  }

  function preferredQueryNode(nodes) {
    const live = nodes.filter((node) => node.kind === "node" && node.status !== "dead");
    const order = ["client", "customer", "authority", "teller", "payment", "visa"];
    for (const id of order) {
      const match = live.find((node) => node.id === id);
      if (match) {
        return match.id;
      }
    }
    return live.length > 0 ? live[0].id : "";
  }

  function queryTargets(nodes, catalog) {
    const map = {};
    nodes.forEach((node) => {
      if (node.kind === "node" && node.status !== "dead") {
        map[node.id] = node;
      }
    });
    catalog.forEach((preset) => {
      if (!preset.running) {
        return;
      }
      if (!map[preset.nodeId]) {
        map[preset.nodeId] = {
          id: preset.nodeId,
          endpoint: preset.endpoint || "",
          programPath: preset.programPath || "",
          pid: preset.pid || null,
          status: "starting",
          kind: "node",
          busy: false,
          currentQuery: "",
          lastResult: null,
          lastSeenAt: Date.now(),
          startedAt: null
        };
      }
    });
    return Object.values(map).sort((lhs, rhs) => lhs.id.localeCompare(rhs.id));
  }

  function apiJson(url, body) {
    return fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(body || {})
    }).then((response) => response.json());
  }

  function detailLine(label, value) {
    return h(
      "div",
      { className: "detail-line", key: label },
      h("span", { className: "detail-label" }, label),
      h("code", { className: "detail-value" }, value || "n/a")
    );
  }

  function MetricChip(label, value) {
    return h(
      "div",
      { className: "stat-chip", key: label },
      h("span", { className: "stat-label" }, label),
      h("strong", { className: "stat-value" }, value)
    );
  }

  function Dashboard() {
    const [snapshot, setSnapshot] = useState(
      normalizeSnapshot({
        startedAt: Date.now(),
        eventsFile: "",
        controlDir: "",
        nodes: [],
        activity: [],
        messages: [],
        catalog: [],
        lastQuery: null
      })
    );
    const [pulses, setPulses] = useState([]);
    const [now, setNow] = useState(Date.now());
    const [activeTab, setActiveTab] = useState("query");
    const [selectedNodeId, setSelectedNodeId] = useState("");
    const [queryText, setQueryText] = useState("");
    const [controlBusy, setControlBusy] = useState("");
    const [controlError, setControlError] = useState("");
    const [controlNote, setControlNote] = useState("");
    const [sidebarOpen, setSidebarOpen] = useState(true);
    const [activityOpen, setActivityOpen] = useState(false);
    const [expandedPresetIds, setExpandedPresetIds] = useState({});
    const [expandedNodeIds, setExpandedNodeIds] = useState({});
    const [expandedResultIds, setExpandedResultIds] = useState({
      artifacts: true,
      answers: false,
      verification: true
    });

    useEffect(() => {
      fetch("/api/state")
        .then((response) => response.json())
        .then((data) => setSnapshot(normalizeSnapshot(data)));

      const source = new EventSource("/api/events");
      source.onmessage = (message) => {
        const event = JSON.parse(message.data);
        setSnapshot((current) => applyDashboardEvent(current, event));
        if (event.type === "request_received") {
          setPulses((current) =>
            current.concat({
              id: `${event.ts}:${event.fromNodeId || "external"}:${event.toNodeId || "node"}`,
              ts: event.ts,
              fromNodeId: event.fromNodeId || "external",
              toNodeId: event.toNodeId || "",
              label: event.op || "rpc"
            })
          );
        }
      };
      return () => source.close();
    }, []);

    useEffect(() => {
      const interval = setInterval(() => {
        setNow(Date.now());
      }, 40);
      return () => clearInterval(interval);
    }, []);

    useEffect(() => {
      setPulses((current) => current.filter((pulse) => now - pulse.ts < 1700));
    }, [now]);

    const presetByNode = useMemo(() => {
      const map = {};
      snapshot.catalog.forEach((preset) => {
        map[preset.nodeId] = preset;
      });
      return map;
    }, [snapshot.catalog]);

    const layout = useMemo(() => graphLayout(snapshot.nodes, 1180, 760), [snapshot.nodes]);
    const liveTargets = useMemo(
      () => snapshot.nodes.filter((node) => node.kind === "node" && node.status !== "dead"),
      [snapshot.nodes]
    );
    const selectableTargets = useMemo(
      () => queryTargets(snapshot.nodes, snapshot.catalog),
      [snapshot.nodes, snapshot.catalog]
    );

    useEffect(() => {
      const preferred = preferredQueryNode(selectableTargets);
      if (!selectedNodeId || !selectableTargets.some((node) => node.id === selectedNodeId)) {
        setSelectedNodeId(preferred);
      }
    }, [selectableTargets, selectedNodeId]);

    const selectedNode = useMemo(
      () => selectableTargets.find((node) => node.id === selectedNodeId) || null,
      [selectableTargets, selectedNodeId]
    );
    const selectedPreset = selectedNode ? presetByNode[selectedNode.id] : null;

    const edges = useMemo(() => {
      const unique = new Map();
      snapshot.messages.slice(0, 60).forEach((message) => {
        if (!message.fromNodeId || !message.toNodeId || message.fromNodeId === message.toNodeId) {
          return;
        }
        const key = `${message.fromNodeId}->${message.toNodeId}`;
        if (!unique.has(key)) {
          unique.set(key, {
            key,
            fromNodeId: message.fromNodeId,
            toNodeId: message.toNodeId
          });
        }
      });
      return Array.from(unique.values());
    }, [snapshot.messages]);

    const groupedCatalog = useMemo(() => {
      const groups = {};
      snapshot.catalog.forEach((preset) => {
        if (!groups[preset.scenario]) {
          groups[preset.scenario] = [];
        }
        groups[preset.scenario].push(preset);
      });
      return Object.keys(groups)
        .sort()
        .map((scenario) => ({
          scenario,
          items: groups[scenario].sort((lhs, rhs) => lhs.label.localeCompare(rhs.label))
        }));
    }, [snapshot.catalog]);

    const runningPresetCount = snapshot.catalog.filter((preset) => preset.running).length;
    const latestActivity = snapshot.activity.length > 0 ? snapshot.activity[0] : null;

    function toggleMapEntry(setter, key) {
      setter((current) =>
        Object.assign({}, current, {
          [key]: !current[key]
        })
      );
    }

    async function refreshFromResponse(promise) {
      setControlError("");
      setControlNote("");
      const data = await promise;
      if (data && data.state) {
        setSnapshot(normalizeSnapshot(data.state));
      }
      if (!data.ok) {
        setControlError(data.error || "operation failed");
        return false;
      }
      if (data.message) {
        setControlNote(data.message);
      }
      return true;
    }

    async function handleStart(presetId) {
      setControlBusy(`start:${presetId}`);
      try {
        await refreshFromResponse(apiJson("/api/nodes/start", { presetId }));
      } finally {
        setControlBusy("");
      }
    }

    async function handleStop(presetId) {
      setControlBusy(`stop:${presetId}`);
      try {
        await refreshFromResponse(apiJson("/api/nodes/stop", { presetId }));
      } finally {
        setControlBusy("");
      }
    }

    async function handleQuerySubmit(event) {
      event.preventDefault();
      if (!selectedNodeId || queryText.trim() === "") {
        setControlError("Select a live node and enter a query.");
        return;
      }
      setControlBusy("query");
      try {
        const ok = await refreshFromResponse(
          apiJson("/api/query", {
            nodeId: selectedNodeId,
            queryText: queryText.trim()
          })
        );
        if (ok) {
          setControlNote("Query finished. Open the artifact drawers below to inspect the certificate and proof paths.");
        }
      } finally {
        setControlBusy("");
      }
    }

    async function handleVerify() {
      setControlBusy("verify");
      try {
        const ok = await refreshFromResponse(apiJson("/api/query/verify", {}));
        if (ok) {
          setControlNote("Proof verification finished.");
        }
      } finally {
        setControlBusy("");
      }
    }

    function renderDisclosure(id, title, summary, children) {
      const open = !!expandedResultIds[id];
      return h(
        "section",
        { className: open ? "disclosure-panel is-open" : "disclosure-panel" },
        h(
          "button",
          {
            type: "button",
            className: "disclosure-toggle",
            onClick: function () {
              toggleMapEntry(setExpandedResultIds, id);
            }
          },
          h(
            "div",
            { className: "disclosure-toggle-copy" },
            h("span", { className: "disclosure-kicker" }, title),
            h("strong", { className: "disclosure-title" }, summary)
          ),
          h("span", { className: "disclosure-chevron" }, open ? "Hide" : "Show")
        ),
        open ? h("div", { className: "disclosure-body" }, children) : null
      );
    }

    function renderPresetCard(preset) {
      const expanded = !!expandedPresetIds[preset.id];
      return h(
        "article",
        {
          key: preset.id,
          className: expanded ? "compact-row is-expanded" : "compact-row"
        },
        h(
          "div",
          { className: "compact-row-head" },
          h(
            "div",
            { className: "compact-row-copy" },
            h(
              "div",
              { className: "compact-title-row" },
              h("strong", null, preset.label),
              h(
                "span",
                { className: preset.running ? "compact-state is-running" : "compact-state" },
                preset.running ? "running" : "stopped"
              )
            ),
            h("p", { className: "compact-summary" }, `${preset.nodeId} · ${preset.endpoint}`)
          ),
          h(
            "div",
            { className: "compact-actions" },
            h(
              "button",
              {
                type: "button",
                className: "secondary-button compact",
                onClick: function () {
                  toggleMapEntry(setExpandedPresetIds, preset.id);
                }
              },
              expanded ? "Hide" : "Details"
            ),
            h(
              "button",
              {
                type: "button",
                className: preset.running ? "secondary-button compact" : "primary-button compact",
                disabled:
                  controlBusy !== "" &&
                  controlBusy !== `start:${preset.id}` &&
                  controlBusy !== `stop:${preset.id}`,
                onClick: function () {
                  if (preset.running) {
                    handleStop(preset.id);
                  } else {
                    handleStart(preset.id);
                  }
                }
              },
              controlBusy === `start:${preset.id}`
                ? "Starting..."
                : controlBusy === `stop:${preset.id}`
                  ? "Stopping..."
                  : preset.running
                    ? "Stop"
                    : "Start"
            )
          )
        ),
        expanded
          ? h(
              "div",
              { className: "compact-row-details" },
              h("p", { className: "node-meta" }, preset.description),
              h("p", { className: "node-meta" }, preset.programPath),
              preset.seeds && preset.seeds.length > 0
                ? h("p", { className: "node-meta" }, `seeds: ${preset.seeds.join(", ")}`)
                : h("p", { className: "node-meta" }, "seeds: none"),
              h("p", { className: "node-meta" }, `recommended query: ${preset.recommendedQuery}`),
              preset.logPath ? h("p", { className: "node-meta" }, `log: ${preset.logPath}`) : null
            )
          : null
      );
    }

    function renderRuntimeNode(node) {
      const expanded = !!expandedNodeIds[node.id];
      return h(
        "article",
        {
          key: node.id,
          className: `compact-row runtime-row tone-${statusTone(node)}${expanded ? " is-expanded" : ""}`
        },
        h(
          "div",
          { className: "compact-row-head" },
          h(
            "div",
            { className: "compact-row-copy" },
            h(
              "div",
              { className: "compact-title-row" },
              h("strong", null, node.id),
              h("span", { className: "pill" }, statusTone(node))
            ),
            h("p", { className: "compact-summary" }, node.endpoint || "external origin")
          ),
          h(
            "div",
            { className: "compact-actions" },
            h(
              "button",
              {
                type: "button",
                className: "secondary-button compact",
                onClick: function () {
                  toggleMapEntry(setExpandedNodeIds, node.id);
                }
              },
              expanded ? "Hide" : "Details"
            )
          )
        ),
        expanded
          ? h(
              "div",
              { className: "compact-row-details" },
              node.programPath ? h("p", { className: "node-meta" }, node.programPath) : null,
              node.currentQuery ? h("p", { className: "node-meta" }, `query: ${node.currentQuery}`) : null,
              node.lastResult
                ? h(
                    "p",
                    { className: "node-meta" },
                    node.lastResult.success
                      ? `${node.lastResult.answerCount} answers, ${node.lastResult.bundleCount} bundles`
                      : node.lastResult.error || "query failed"
                  )
                : h("p", { className: "node-meta" }, "No query result yet.")
            )
          : null
      );
    }

    function renderQueryPane() {
      return h(
        "div",
        { className: "sidebar-scroll query-pane" },
        h(
          "section",
          { className: "sidebar-section sidebar-intro" },
          h("span", { className: "section-kicker" }, "Query Runner"),
          h("h3", { className: "section-title" }, "Send a live query into the cluster"),
          h(
            "p",
            { className: "section-subtitle" },
            "Choose a client-facing node, run the query, then inspect the certificate and proof artifacts below."
          )
        ),
        h(
          "form",
          { className: "query-form", onSubmit: handleQuerySubmit },
          h(
            "label",
            { className: "field-label" },
            "Target Node",
            h(
              "select",
              {
                className: "field-control",
                value: selectedNodeId,
                onChange: function (event) {
                  setSelectedNodeId(event.target.value);
                }
              },
              selectableTargets.length === 0
                ? h("option", { value: "" }, "Start a node first")
                : selectableTargets.map((node) =>
                    h("option", { key: node.id, value: node.id }, `${node.id} (${node.endpoint})`)
                  )
            )
          ),
          h(
            "label",
            { className: "field-label" },
            "Command Line",
            h(
              "div",
              { className: "cli-shell" },
              h(
                "span",
                { className: "cli-prefix" },
                selectedNode ? `etbctl query ${selectedNode.endpoint}` : "etbctl query <node>"
              ),
              h("input", {
                className: "cli-input",
                type: "text",
                value: queryText,
                placeholder:
                  selectedPreset && selectedPreset.recommendedQuery
                    ? selectedPreset.recommendedQuery
                    : "trip_ready(alice)",
                onChange: function (event) {
                  setQueryText(event.target.value);
                }
              }),
              selectedPreset && selectedPreset.recommendedQuery
                ? h("span", { className: "field-hint" }, `Suggested: ${selectedPreset.recommendedQuery}`)
                : null
            )
          ),
          h(
            "div",
            { className: "button-row" },
            h(
              "button",
              {
                type: "submit",
                className: "primary-button",
                disabled: controlBusy !== "" || !selectedNodeId || queryText.trim() === ""
              },
              controlBusy === "query" ? "Running..." : "Send Query"
            ),
            h(
              "button",
              {
                type: "button",
                className: "secondary-button",
                disabled: controlBusy !== "" || !snapshot.lastQuery || !snapshot.lastQuery.success,
                onClick: handleVerify
              },
              controlBusy === "verify" ? "Checking..." : "Run Proof Checker"
            )
          )
        ),
        snapshot.lastQuery
          ? h(
              "div",
              { className: "result-stack" },
              h(
                "section",
                { className: "result-summary-card" },
                h(
                  "div",
                  { className: "result-summary-head" },
                  h("span", { className: "section-kicker" }, "Last Query"),
                  h(
                    "strong",
                    { className: "result-summary-title" },
                    `${snapshot.lastQuery.nodeId} · ${snapshot.lastQuery.queryText}`
                  )
                ),
                h(
                  "div",
                  { className: "result-summary-grid" },
                  MetricChip("Answers", String(snapshot.lastQuery.answers ? snapshot.lastQuery.answers.length : 0)),
                  MetricChip("Bundles", String(snapshot.lastQuery.bundles || 0)),
                  MetricChip("Status", snapshot.lastQuery.success ? "ok" : "failed")
                )
              ),
              renderDisclosure(
                "artifacts",
                "Artifacts",
                snapshot.lastQuery.outputDir || "Query output paths",
                h(
                  "div",
                  { className: "result-grid" },
                  detailLine("Node", snapshot.lastQuery.nodeId),
                  detailLine("Endpoint", snapshot.lastQuery.endpoint),
                  detailLine("Output Dir", snapshot.lastQuery.outputDir),
                  detailLine("Certificate", snapshot.lastQuery.certificatePath || "not written"),
                  detailLine("Proof", snapshot.lastQuery.proofPath || "not written"),
                  detailLine("Bundle Dir", snapshot.lastQuery.bundleDir || "n/a"),
                  detailLine("stdout", snapshot.lastQuery.stdoutPath || "n/a"),
                  detailLine("stderr", snapshot.lastQuery.stderrPath || "n/a")
                )
              ),
              snapshot.lastQuery.answers && snapshot.lastQuery.answers.length > 0
                ? renderDisclosure(
                    "answers",
                    "Answers",
                    `${snapshot.lastQuery.answers.length} derived answers`,
                    h(
                      "div",
                      { className: "answer-list" },
                      snapshot.lastQuery.answers.map((answer) =>
                        h("code", { key: answer, className: "answer-pill" }, answer)
                      )
                    )
                  )
                : null,
              snapshot.lastQuery.verification
                ? renderDisclosure(
                    "verification",
                    "Verification",
                    snapshot.lastQuery.verification.success
                      ? "Proof verification passed"
                      : "Proof verification failed",
                    h(
                      "div",
                      { className: "verification-block" },
                      h(
                        "div",
                        {
                          className: snapshot.lastQuery.verification.success
                            ? "status-banner status-ok"
                            : "status-banner status-error"
                        },
                        snapshot.lastQuery.verification.success
                          ? "Proof verification passed."
                          : "Proof verification failed."
                      ),
                      snapshot.lastQuery.verification.results.map((entry) =>
                        h(
                          "div",
                          { key: entry.label, className: "verify-row" },
                          h("strong", null, entry.label),
                          h("span", null, entry.success ? "ok" : "failed"),
                          h("code", null, entry.proofPath)
                        )
                      )
                    )
                  )
                : null
            )
          : h(
              "div",
              { className: "empty-state" },
              selectableTargets.length > 0
                ? `Ready to query ${selectedNodeId || "a live node"}. The proof and certificate drawers will appear here after the first run.`
                : "No dashboard query has been run yet. Start a client or customer node, then send a query from this pane."
            )
      );
    }

    function renderNodesPane() {
      return h(
        "div",
        { className: "sidebar-scroll launch-pane" },
        groupedCatalog.map((group) =>
          h(
            "section",
            { key: group.scenario, className: "sidebar-section" },
            h(
              "div",
              { className: "section-heading-row" },
              h("h3", { className: "section-title" }, group.scenario),
              h("span", { className: "section-count" }, `${group.items.length} nodes`)
            ),
            h(
              "div",
              { className: "compact-list" },
              group.items.map(renderPresetCard)
            )
          )
        ),
        h(
          "section",
          { className: "sidebar-section" },
          h(
            "div",
            { className: "section-heading-row" },
            h("h3", { className: "section-title" }, "Runtime"),
            h("span", { className: "section-count" }, `${snapshot.nodes.length} visible`)
          ),
          snapshot.nodes.length > 0
            ? h("div", { className: "compact-list" }, snapshot.nodes.map(renderRuntimeNode))
            : h("div", { className: "empty-state compact-empty" }, "No nodes are visible yet.")
        )
      );
    }

    return h(
      "div",
      { className: "page-shell" },
      h(
        "header",
        { className: "topbar panel-surface" },
        h(
          "div",
          { className: "topbar-brand" },
          h("p", { className: "eyebrow" }, "zk-ETB"),
          h("h1", null, "Distributed Proof Dashboard"),
          h(
            "p",
            { className: "topbar-subtext" },
            "Watch topology updates in real time, route live queries, and verify proof artifacts without losing the cluster view."
          )
        ),
        h(
          "div",
          { className: "topbar-stats" },
          MetricChip("Live Nodes", String(liveTargets.length)),
          MetricChip("Running Presets", String(runningPresetCount)),
          MetricChip("Messages", String(snapshot.messages.length))
        ),
        h(
          "div",
          { className: "topbar-actions" },
          h(
            "button",
            {
              type: "button",
              className: "secondary-button",
              onClick: function () {
                setActivityOpen(!activityOpen);
              }
            },
            activityOpen ? "Hide Logs" : "Show Logs"
          ),
          h(
            "button",
            {
              type: "button",
              className: sidebarOpen ? "secondary-button" : "primary-button",
              onClick: function () {
                setSidebarOpen(!sidebarOpen);
              }
            },
            sidebarOpen ? "Hide Controls" : "Open Controls"
          )
        )
      ),
      h(
        "main",
        { className: sidebarOpen ? "workspace sidebar-open" : "workspace sidebar-closed" },
        h(
          "section",
          { className: "main-stage" },
          h(
            "section",
            { className: "topology-card panel-surface" },
            h(
              "div",
              { className: "panel-heading topology-heading" },
              h(
                "div",
                null,
                h("h2", null, "Topology"),
                h("p", null, "Live peers stay on the canvas while animated dots trace active communication.")
              ),
              h(
                "div",
                { className: "topology-meta" },
                MetricChip("Events", String(snapshot.activity.length)),
                MetricChip("Query Targets", String(selectableTargets.length))
              )
            ),
            h(
              "svg",
              { className: "network-canvas", viewBox: "0 0 1180 760" },
              h(
                "defs",
                null,
                h(
                  "radialGradient",
                  { id: "panelGlow" },
                  h("stop", { offset: "0%", stopColor: "rgba(255,255,255,0.20)" }),
                  h("stop", { offset: "100%", stopColor: "rgba(255,255,255,0)" })
                )
              ),
              h("rect", { x: 0, y: 0, width: 1180, height: 760, fill: "url(#panelGlow)" }),
              edges.map((edge) => {
                const from = layout[edge.fromNodeId];
                const to = layout[edge.toNodeId];
                if (!from || !to) {
                  return null;
                }
                return h("line", {
                  key: edge.key,
                  x1: from.x,
                  y1: from.y,
                  x2: to.x,
                  y2: to.y,
                  className: "edge-line"
                });
              }),
              pulses.map((pulse) => {
                const from = layout[pulse.fromNodeId];
                const to = layout[pulse.toNodeId];
                if (!from || !to) {
                  return null;
                }
                const progress = Math.max(0, Math.min(1, (now - pulse.ts) / 1500));
                const x = from.x + (to.x - from.x) * progress;
                const y = from.y + (to.y - from.y) * progress;
                return h(
                  "g",
                  { key: pulse.id },
                  h("circle", { cx: x, cy: y, r: 7, className: "pulse-dot" }),
                  progress < 0.9
                    ? h("text", { x: x + 12, y: y - 10, className: "pulse-label" }, pulse.label)
                    : null
                );
              }),
              snapshot.nodes.map((node) => {
                const position = layout[node.id];
                if (!position) {
                  return null;
                }
                const tone = statusTone(node);
                return h(
                  "g",
                  { key: node.id, transform: `translate(${position.x}, ${position.y})` },
                  h("circle", { r: node.kind === "external" ? 38 : 52, className: `node-core node-${tone}` }),
                  h("circle", { r: node.kind === "external" ? 52 : 68, className: "node-halo" }),
                  h("text", { y: -8, className: "node-title", textAnchor: "middle" }, node.id),
                  h("text", { y: 17, className: "node-subtitle", textAnchor: "middle" }, node.endpoint || node.status),
                  node.currentQuery
                    ? h("text", { y: 62, className: "node-query", textAnchor: "middle" }, node.currentQuery)
                    : null
                );
              })
            )
          ),
          h(
            "section",
            {
              className: activityOpen
                ? "activity-shell panel-surface is-open"
                : "activity-shell panel-surface"
            },
            h(
              "button",
              {
                type: "button",
                className: "disclosure-toggle activity-toggle",
                onClick: function () {
                  setActivityOpen(!activityOpen);
                }
              },
              h(
                "div",
                { className: "disclosure-toggle-copy" },
                h("span", { className: "disclosure-kicker" }, "Activity"),
                h(
                  "strong",
                  { className: "disclosure-title" },
                  latestActivity ? latestActivity.title : "No activity yet"
                ),
                h(
                  "span",
                  { className: "activity-summary" },
                  latestActivity
                    ? `${formatTime(latestActivity.ts)} · ${latestActivity.detail || "Recent lifecycle and query events"}`
                    : "Open the log drawer to inspect node lifecycle events and Datalog invocations."
                )
              ),
              h("span", { className: "disclosure-chevron" }, activityOpen ? "Hide" : "Show")
            ),
            activityOpen
              ? h(
                  "div",
                  { className: "activity-list" },
                  snapshot.activity.map((entry) =>
                    h(
                      "article",
                      { key: entry.id, className: `activity-row activity-${activityTone(entry)}` },
                      h("span", { className: "activity-time" }, formatTime(entry.ts)),
                      h(
                        "div",
                        { className: "activity-copy" },
                        h("strong", null, entry.title),
                        h("p", null, entry.detail)
                      )
                    )
                  )
                )
              : null
          )
        ),
        h(
          "aside",
          { className: sidebarOpen ? "sidebar sidebar-open" : "sidebar sidebar-closed" },
          h(
            "div",
            { className: "sidebar-shell panel-surface" },
            h(
              "div",
              { className: "sidebar-header" },
              h(
                "div",
                null,
                h("span", { className: "section-kicker" }, "Control Panel"),
                h("h2", null, activeTab === "query" ? "Query" : "Nodes"),
                h(
                  "p",
                  null,
                  activeTab === "query"
                    ? "Run a query and inspect the returned proof bundle."
                    : "Start or stop nodes and inspect live node state."
                )
              ),
              h(
                "button",
                {
                  type: "button",
                  className: "secondary-button compact",
                  onClick: function () {
                    setSidebarOpen(false);
                  }
                },
                "Collapse"
              )
            ),
            h(
              "div",
              { className: "sidebar-tabbar" },
              h(
                "button",
                {
                  type: "button",
                  className: activeTab === "query" ? "tab-button is-active" : "tab-button",
                  onClick: function () {
                    setActiveTab("query");
                  }
                },
                "Query"
              ),
              h(
                "button",
                {
                  type: "button",
                  className: activeTab === "nodes" ? "tab-button is-active" : "tab-button",
                  onClick: function () {
                    setActiveTab("nodes");
                  }
                },
                "Nodes"
              )
            ),
            controlError ? h("div", { className: "status-banner status-error" }, controlError) : null,
            !controlError && controlNote ? h("div", { className: "status-banner status-note" }, controlNote) : null,
            activeTab === "query" ? renderQueryPane() : renderNodesPane()
          )
        )
      )
    );
  }

  const mountNode = document.getElementById("root");
  if (ReactDOM.createRoot) {
    ReactDOM.createRoot(mountNode).render(h(Dashboard));
  } else {
    ReactDOM.render(h(Dashboard), mountNode);
  }
})();
