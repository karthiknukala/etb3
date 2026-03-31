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
          kind: "request_received",
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
      const radius = Math.min(width, height) * 0.3;
      positions[node.id] = {
        x: width / 2 + Math.cos(angle) * radius,
        y: height / 2 + Math.sin(angle) * radius
      };
    });

    external.forEach((node, index) => {
      positions[node.id] = {
        x: 110,
        y: 120 + index * 110
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

    useEffect(() => {
      const preferred = preferredQueryNode(snapshot.nodes);
      if (!selectedNodeId || !snapshot.nodes.some((node) => node.id === selectedNodeId && node.status !== "dead")) {
        setSelectedNodeId(preferred);
      }
    }, [snapshot.nodes, selectedNodeId]);

    const presetByNode = useMemo(() => {
      const map = {};
      snapshot.catalog.forEach((preset) => {
        map[preset.nodeId] = preset;
      });
      return map;
    }, [snapshot.catalog]);

    useEffect(() => {
      if (!selectedNodeId || queryText.trim() !== "") {
        return;
      }
      const preset = presetByNode[selectedNodeId];
      if (preset && preset.recommendedQuery) {
        setQueryText(preset.recommendedQuery);
      }
    }, [selectedNodeId, presetByNode, queryText]);

    const layout = useMemo(() => graphLayout(snapshot.nodes, 980, 680), [snapshot.nodes]);
    const liveTargets = useMemo(
      () => snapshot.nodes.filter((node) => node.kind === "node" && node.status !== "dead"),
      [snapshot.nodes]
    );
    const selectedNode = useMemo(
      () => liveTargets.find((node) => node.id === selectedNodeId) || null,
      [liveTargets, selectedNodeId]
    );
    const selectedPreset = selectedNode ? presetByNode[selectedNode.id] : null;

    const edges = useMemo(() => {
      const unique = new Map();
      snapshot.messages.slice(0, 40).forEach((message) => {
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
          setControlNote("Query finished. The certificate and proof paths are listed below.");
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

    return h(
      "div",
      { className: "page-shell" },
      h(
        "header",
        { className: "hero" },
        h(
          "div",
          { className: "hero-copy" },
          h("p", { className: "eyebrow" }, "Live zk-ETB Topology"),
          h("h1", null, "Nodes, routes, proofs, and a control deck"),
          h(
            "p",
            { className: "hero-text" },
            "Start example nodes from the dashboard, send a live query into the client or customer node, and verify the returned certificate and proof bundle without leaving the UI."
          )
        ),
        h(
          "div",
          { className: "hero-meta" },
          h("div", { className: "meta-card" }, h("span", { className: "meta-label" }, "Events File"), h("strong", null, snapshot.eventsFile || "n/a")),
          h("div", { className: "meta-card" }, h("span", { className: "meta-label" }, "Control Dir"), h("strong", null, snapshot.controlDir || "n/a")),
          h("div", { className: "meta-card" }, h("span", { className: "meta-label" }, "Live Nodes"), h("strong", null, String(liveTargets.length)))
        )
      ),
      h(
        "main",
        { className: "dashboard-grid" },
        h(
          "section",
          { className: "graph-panel" },
          h("div", { className: "panel-heading" }, h("h2", null, "Topology"), h("p", null, "Dots animate on incoming communication events.")),
          h(
            "svg",
            { className: "network-canvas", viewBox: "0 0 980 680" },
            h("defs", null,
              h("radialGradient", { id: "panelGlow" },
                h("stop", { offset: "0%", stopColor: "rgba(255,255,255,0.22)" }),
                h("stop", { offset: "100%", stopColor: "rgba(255,255,255,0)" })
              )
            ),
            h("rect", { x: 0, y: 0, width: 980, height: 680, fill: "url(#panelGlow)" }),
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
                h("circle", { r: node.kind === "external" ? 38 : 50, className: `node-core node-${tone}` }),
                h("circle", { r: node.kind === "external" ? 52 : 66, className: "node-halo" }),
                h("text", { y: -6, className: "node-title", textAnchor: "middle" }, node.id),
                h("text", { y: 16, className: "node-subtitle", textAnchor: "middle" }, node.endpoint || node.status),
                node.currentQuery
                  ? h("text", { y: 58, className: "node-query", textAnchor: "middle" }, node.currentQuery)
                  : null
              );
            })
          )
        ),
        h(
          "section",
          { className: "side-panel" },
          h(
            "div",
            { className: "card control-card" },
            h("div", { className: "panel-heading" }, h("h2", null, "Command Deck"), h("p", null, "Launch nodes, send live queries, and verify proofs.")),
            h(
              "div",
              { className: "tab-row" },
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
            activeTab === "query"
              ? h(
                  "div",
                  { className: "query-pane" },
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
                            if (presetByNode[event.target.value] && presetByNode[event.target.value].recommendedQuery) {
                              setQueryText(presetByNode[event.target.value].recommendedQuery);
                            }
                          }
                        },
                        liveTargets.length === 0
                          ? h("option", { value: "" }, "Start a node first")
                          : liveTargets.map((node) =>
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
                        h("span", { className: "cli-prefix" }, selectedNode ? `etbctl query ${selectedNode.endpoint}` : "etbctl query <node>"),
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
                        })
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
                        { className: "result-block" },
                        h("h3", null, "Last Query Result"),
                        h(
                          "div",
                          { className: "result-grid" },
                          detailLine("Node", snapshot.lastQuery.nodeId),
                          detailLine("Endpoint", snapshot.lastQuery.endpoint),
                          detailLine("Query", snapshot.lastQuery.queryText),
                          detailLine("Output Dir", snapshot.lastQuery.outputDir),
                          detailLine("Certificate", snapshot.lastQuery.certificatePath || "not written"),
                          detailLine("Proof", snapshot.lastQuery.proofPath || "not written"),
                          detailLine("Bundle Dir", snapshot.lastQuery.bundleDir || "n/a"),
                          detailLine("stdout", snapshot.lastQuery.stdoutPath || "n/a"),
                          detailLine("stderr", snapshot.lastQuery.stderrPath || "n/a")
                        ),
                        snapshot.lastQuery.answers && snapshot.lastQuery.answers.length > 0
                          ? h(
                              "div",
                              { className: "answer-list" },
                              snapshot.lastQuery.answers.map((answer) =>
                                h("code", { key: answer, className: "answer-pill" }, answer)
                              )
                            )
                          : null,
                        snapshot.lastQuery.verification
                          ? h(
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
                          : null
                      )
                    : h(
                        "div",
                        { className: "empty-state" },
                        "No dashboard query has been run yet. Start a client or customer node, then send a query from this pane."
                      )
                )
              : h(
                  "div",
                  { className: "launch-pane" },
                  groupedCatalog.map((group) =>
                    h(
                      "section",
                      { key: group.scenario, className: "preset-group" },
                      h("h3", null, group.scenario),
                      group.items.map((preset) =>
                        h(
                          "article",
                          { key: preset.id, className: "preset-card" },
                          h(
                            "div",
                            { className: "preset-head" },
                            h(
                              "div",
                              null,
                              h("strong", null, `${preset.label} (${preset.nodeId})`),
                              h("p", { className: "preset-copy" }, preset.description)
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
                          ),
                          h("p", { className: "node-endpoint" }, preset.endpoint),
                          h("p", { className: "node-meta" }, preset.programPath),
                          preset.seeds && preset.seeds.length > 0
                            ? h("p", { className: "node-meta" }, `seeds: ${preset.seeds.join(", ")}`)
                            : h("p", { className: "node-meta" }, "seeds: none"),
                          h("p", { className: "node-meta" }, `recommended query: ${preset.recommendedQuery}`),
                          preset.logPath ? h("p", { className: "node-meta" }, `log: ${preset.logPath}`) : null
                        )
                      )
                    )
                  )
                )
          ),
          h(
            "div",
            { className: "card" },
            h("div", { className: "panel-heading" }, h("h2", null, "Nodes"), h("p", null, "Runtime status, endpoints, and recent proof results.")),
            h(
              "div",
              { className: "node-list" },
              snapshot.nodes.map((node) =>
                h(
                  "article",
                  { key: node.id, className: `node-card tone-${statusTone(node)}` },
                  h("div", { className: "node-card-top" },
                    h("strong", null, node.id),
                    h("span", { className: "pill" }, statusTone(node))
                  ),
                  h("p", { className: "node-endpoint" }, node.endpoint || "external origin"),
                  node.programPath ? h("p", { className: "node-meta" }, node.programPath) : null,
                  node.lastResult
                    ? h(
                        "p",
                        { className: "node-meta" },
                        node.lastResult.success
                          ? `${node.lastResult.answerCount} answers, ${node.lastResult.bundleCount} bundles`
                          : node.lastResult.error || "query failed"
                      )
                    : null
                )
              )
            )
          ),
          h(
            "div",
            { className: "card" },
            h("div", { className: "panel-heading" }, h("h2", null, "Activity"), h("p", null, "Most recent lifecycle and transaction events.")),
            h(
              "div",
              { className: "activity-list" },
              snapshot.activity.map((entry) =>
                h(
                  "article",
                  { key: entry.id, className: "activity-row" },
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
