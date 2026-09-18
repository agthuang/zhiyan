(() => {
  const list = document.getElementById("list");
  const empty = document.getElementById("empty");
  const conn = document.getElementById("conn");
  const stateEl = document.getElementById("state");
  const sessionEl = document.getElementById("session");
  const btnClear = document.getElementById("btnClear");

  const seen = new Set();
  let es = null;

  function setConn(ok) {
    conn.textContent = ok ? "SSE 已连接" : "未连接";
    conn.classList.toggle("on", ok);
    conn.classList.toggle("off", !ok);
  }

  function formatTime(iso) {
    if (!iso) return "—";
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return iso;
    return d.toLocaleTimeString("zh-CN", { hour12: false });
  }

  function renderStatus(status) {
    if (!status || typeof status !== "object") return;
    stateEl.textContent = `state ${status.state || "—"}`;
    const sid = status.session_id ? String(status.session_id).slice(0, 8) : "—";
    sessionEl.textContent = `session ${sid}`;
  }

  function eventKey(ev) {
    return [
      ev.received_at || "",
      ev.hook_event_name || "",
      ev.session_id || "",
      ev.tool_name || "",
      ev.turn_id || "",
      ev.suppressed ? "1" : "0",
    ].join("|");
  }

  function detailText(ev) {
    const bits = [];
    if (ev.tool_name) bits.push(`tool ${ev.tool_name}`);
    if (ev.tool_input_preview) {
      const preview = Object.entries(ev.tool_input_preview)
        .map(([k, v]) => `${k}=${v}`)
        .join(" · ");
      if (preview) bits.push(preview);
    }
    if (ev.cwd) bits.push(ev.cwd);
    if (ev.model) bits.push(ev.model);
    return bits.join("  ·  ");
  }

  function addEvent(ev, { prepend = true } = {}) {
    const key = eventKey(ev);
    if (seen.has(key)) return;
    seen.add(key);

    const li = document.createElement("li");
    li.className = "item";

    const time = document.createElement("div");
    time.className = "time";
    time.textContent = formatTime(ev.received_at);

    const body = document.createElement("div");
    const top = document.createElement("div");
    top.className = "row-top";

    const name = document.createElement("span");
    name.className = "event";
    name.textContent = ev.hook_event_name || "(unknown)";
    top.appendChild(name);

    if (ev.state) {
      const tag = document.createElement("span");
      tag.className = `tag state-${ev.state}`;
      tag.textContent = ev.state;
      top.appendChild(tag);
    }
    if (ev.suppressed) {
      const tag = document.createElement("span");
      tag.className = "tag suppressed";
      tag.textContent = "suppressed";
      top.appendChild(tag);
    }

    body.appendChild(top);

    const detail = detailText(ev);
    if (detail) {
      const d = document.createElement("div");
      d.className = "detail";
      const code = document.createElement("code");
      code.textContent = detail;
      d.appendChild(code);
      body.appendChild(d);
    }

    li.appendChild(time);
    li.appendChild(body);

    if (prepend) list.prepend(li);
    else list.appendChild(li);

    empty.classList.add("hidden");
  }

  async function loadHistory() {
    try {
      const res = await fetch("/api/events?limit=150");
      const events = await res.json();
      // API returns chronological order; show newest first.
      for (const ev of events.slice().reverse()) {
        addEvent(ev, { prepend: false });
      }
    } catch (_) {
      /* ignore */
    }
  }

  async function loadStatus() {
    try {
      const res = await fetch("/api/status");
      renderStatus(await res.json());
    } catch (_) {
      /* ignore */
    }
  }

  function connect() {
    if (es) es.close();
    es = new EventSource("/events");
    es.addEventListener("open", () => setConn(true));
    es.addEventListener("error", () => setConn(false));
    es.addEventListener("hook", (msg) => {
      try {
        addEvent(JSON.parse(msg.data), { prepend: true });
      } catch (_) {
        /* ignore */
      }
    });
    es.addEventListener("status", (msg) => {
      try {
        renderStatus(JSON.parse(msg.data));
      } catch (_) {
        /* ignore */
      }
    });
  }

  btnClear.addEventListener("click", () => {
    list.innerHTML = "";
    seen.clear();
    empty.classList.remove("hidden");
  });

  loadHistory().then(loadStatus).then(connect);
})();
