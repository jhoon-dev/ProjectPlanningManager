"use strict";
const $ = (id) => document.getElementById(id);
let token = "",
  account = null,
  current = null,
  preview = null,
  dirty = false,
  cursor = 0,
  socket = null,
  retry = null,
  stopped = true,
  draftGeneration = 0;
const message = (text) => {
  $("status").textContent = text;
};
function invalidate() {
  preview = null;
  $("apply").disabled = true;
}
async function api(path, options = {}) {
  const headers = { ...options.headers };
  if (token) headers.Authorization = "Bearer " + token;
  if (options.json !== undefined) {
    headers["Content-Type"] = "application/json";
    options.body = JSON.stringify(options.json);
    delete options.json;
  }
  const response = await fetch("/api/v1" + path, {
    ...options,
    headers,
    cache: "no-store",
    credentials: "omit",
  });
  const result =
    options.raw && response.ok ? await response.blob() : await response.json();
  if (!response.ok) {
    if (response.status === 401) {
      stopped = true;
      socket?.close();
      $("loginPanel").hidden = false;
      $("workspace").hidden = true;
      token = "";
      $("password").value = "";
    }
    throw new Error(result.error?.message || "요청 실패");
  }
  return result;
}
function action(id, fn) {
  $(id).addEventListener("click", () =>
    Promise.resolve()
      .then(fn)
      .catch((e) => message(e.message)),
  );
}
async function projects() {
  const r = await api("/projects");
  const selected = $("project").value;
  $("project").replaceChildren();
  for (const p of r.projects) {
    const option = document.createElement("option");
    option.value = p.id;
    option.textContent = p.name + " · v" + p.version;
    $("project").append(option);
  }
  if ([...$("project").options].some((o) => o.value === selected))
    $("project").value = selected;
}
async function users() {
  if (!account.admin) return;
  const r = await api("/users");
  $("grantUser").replaceChildren();
  for (const u of r.users) {
    const o = document.createElement("option");
    o.value = u.id;
    o.textContent = u.id + (u.enabled ? "" : " (비활성)");
    $("grantUser").append(o);
  }
}
async function openProject(force = false) {
  const id = $("project").value;
  if (!id) throw new Error("프로젝트를 선택하세요.");
  if (dirty && !force)
    throw new Error(
      "아직 적용하지 않은 변경이 있습니다. 미리보기로 검토하거나 ‘서버 원본 다시 읽기’를 선택하세요.",
    );
  const epoch = draftGeneration;
  const r = await api("/projects/" + encodeURIComponent(id));
  if (epoch !== draftGeneration) {
    $("conflict").textContent =
      "서버 응답을 기다리는 동안 입력이 변경되어 보존했습니다.";
    return;
  }
  if (current?.id === r.id && current.version > r.version) return;
  current = r;
  dirty = false;
  invalidate();
  $("conflict").textContent = "";
  $("source").textContent =
    r.name +
    " · 서버 버전 " +
    r.version +
    " · 입력 revision " +
    r.summary.revision;
  $("tasks").replaceChildren();
  for (const task of r.summary.tasks) {
    const row = document.createElement("tr");
    row.dataset.id = task.taskId;
    const name = document.createElement("input");
    name.value = task.name;
    name.setAttribute("aria-label", "작업 이름");
    const duration = document.createElement("input");
    duration.type = "number";
    duration.min = "0";
    duration.max = "5000000";
    duration.value = task.durationMinutes ?? "";
    duration.setAttribute("aria-label", "기간 근무 분");
    for (const input of [name, duration])
      input.addEventListener("input", () => {
        dirty = true;
        draftGeneration++;
        invalidate();
      });
    for (const content of [name, duration, task.taskId]) {
      const cell = document.createElement("td");
      if (typeof content === "string") cell.textContent = content;
      else cell.append(content);
      row.append(cell);
    }
    $("tasks").append(row);
  }
  message("서버 원본을 읽었습니다.");
}
function batch() {
  if (!current) throw new Error("프로젝트를 먼저 여세요.");
  const originals = new Map(current.summary.tasks.map((t) => [t.taskId, t]));
  const operations = [];
  for (const row of $("tasks").children) {
    const [name, duration] = row.querySelectorAll("input");
    const before = originals.get(row.dataset.id);
    const op = { taskId: before.taskId };
    if (name.value !== before.name) op.name = name.value;
    const n = duration.value === "" ? null : Number(duration.value);
    if (n !== null && (!Number.isSafeInteger(n) || n < 0 || n > 5000000))
      throw new Error("기간은 0~5000000의 정수 분입니다.");
    if (n !== before.durationMinutes) op.durationMinutes = n;
    if (Object.keys(op).length > 1) operations.push(op);
  }
  if (!operations.length) throw new Error("변경된 작업이 없습니다.");
  return {
    expectedVersion: current.version,
    request: {
      format: "PPM-AUTOMATION-1",
      documentId: current.id,
      revision: current.summary.revision,
      requestId: crypto.randomUUID
        ? crypto.randomUUID()
        : "request-" + Date.now() + "-" + Math.random().toString(16).slice(2),
      operations,
    },
  };
}
function stream() {
  if (stopped) return;
  const url = new URL("/api/v1/stream", location.href);
  url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(url);
  socket = ws;
  ws.onopen = () => {
    ws.send(JSON.stringify({ token, after: cursor }));
  };
  ws.onmessage = async (event) => {
    try {
      const m = JSON.parse(event.data);
      if (m.type === "connected") {
        $("connection").textContent = "실시간 연결됨";
        return;
      }
      if (m.type !== "changes") return;
      cursor = m.cursor;
      for (const change of m.events) {
        if (
          current &&
          change.projectId === current.id &&
          (change.kind !== "project-changed" ||
            change.version > current.version) &&
          (change.kind === "project-changed" ||
            change.kind === "permission-changed")
        ) {
          invalidate();
          if (dirty) {
            $("conflict").textContent =
              "서버 원본 또는 권한이 변경되었습니다. 작성 중인 내용은 유지했습니다. 다시 읽은 원본과 대조한 뒤 수정하세요.";
          } else {
            await openProject();
          }
        }
      }
      if (
        m.events.some(
          (e) =>
            e.kind === "project-created" || e.kind === "permission-changed",
        )
      )
        await projects();
    } catch (e) {
      message(e.message);
    }
  };
  ws.onclose = (e) => {
    if (e.code === 1008) {
      stopped = true;
      token = "";
      $("loginPanel").hidden = false;
      $("workspace").hidden = true;
      message("세션이 만료되거나 해제되었습니다. 다시 로그인하세요.");
    }
    $("connection").textContent = stopped ? "연결 종료" : "연결 재시도 중";
    if (!stopped) retry = setTimeout(stream, 2000);
  };
}
$("loginForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  try {
    const oldUser = account?.user;
    const r = await api("/login", {
      method: "POST",
      json: { username: $("username").value, password: $("password").value },
    });
    token = r.token;
    account = r;
    $("password").value = "";
    $("loginPanel").hidden = true;
    $("workspace").hidden = false;
    $("adminPanel").hidden = !r.admin;
    $("identity").textContent = r.user + (r.admin ? " · 서버 관리자" : "");
    if (oldUser !== r.user) {
      current = null;
      dirty = false;
      $("tasks").replaceChildren();
      cursor = 0;
    }
    stopped = false;
    await projects();
    await users();
    await jobs();
    clearTimeout(retry);
    socket?.close();
    stream();
    message("로그인했습니다.");
  } catch (error) {
    message(error.message);
  }
});
action("logout", async () => {
  await api("/logout", { method: "POST" });
  stopped = true;
  clearTimeout(retry);
  socket?.close();
  token = "";
  account = null;
  current = null;
  dirty = false;
  invalidate();
  $("tasks").replaceChildren();
  $("receipt").textContent = "";
  $("loginPanel").hidden = false;
  $("workspace").hidden = true;
  message("로그아웃했습니다.");
});
action("reload", projects);
action("open", () => openProject());
action("refresh", () => openProject(true));
action("preview", async () => {
  invalidate();
  const data = batch();
  const epoch = draftGeneration;
  const prepared = await api(
    "/projects/" + encodeURIComponent(current.id) + "/preview",
    { method: "POST", json: data },
  );
  if (epoch !== draftGeneration) {
    invalidate();
    throw new Error("입력이 변경되었습니다. 미리보기를 다시 실행하세요.");
  }
  preview = prepared;
  $("receipt").textContent = JSON.stringify(preview.receipt, null, 2);
  $("apply").disabled = false;
  message("변경 전후를 확인한 뒤 적용하세요.");
});
action("apply", async () => {
  if (!preview || !current) throw new Error("미리보기를 다시 실행하세요.");
  const ticket = preview.previewId;
  invalidate();
  const controls = [
    ...$("workspace").querySelectorAll("input,select,button"),
  ].map((node) => [node, node.disabled]);
  for (const [node] of controls) node.disabled = true;
  try {
    const receipt = await api(
      "/projects/" + encodeURIComponent(current.id) + "/apply",
      { method: "POST", json: { previewId: ticket } },
    );
    dirty = false;
    $("receipt").textContent = JSON.stringify(receipt, null, 2);
    await openProject(true);
    await projects();
    message("변경을 서버에 원자적으로 저장했습니다.");
  } finally {
    for (const [node, disabled] of controls)
      if (node.isConnected) node.disabled = disabled;
  }
});
action("download", async () => {
  if (!current) throw new Error("프로젝트를 여세요.");
  const blob = await api(
    "/projects/" + encodeURIComponent(current.id) + "/native",
    { raw: true },
  );
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "project.ppm";
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
});
action("import", async () => {
  const file = $("upload").files[0];
  if (!file) throw new Error(".ppm 파일을 선택하세요.");
  if (file.size > 64 * 1024 * 1024)
    throw new Error("64 MiB 이하 파일을 선택하세요.");
  await api("/projects", {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: file,
  });
  await projects();
  message("프로젝트를 등록했습니다.");
});
$("userForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  try {
    await api("/users", {
      method: "POST",
      json: { id: $("newUser").value, password: $("newPassword").value },
    });
    $("newPassword").value = "";
    await users();
    message("계정을 생성했습니다.");
  } catch (error) {
    message(error.message);
  }
});
action("grant", async () => {
  if (!$("project").value) throw new Error("프로젝트를 선택하세요.");
  await api("/projects/" + encodeURIComponent($("project").value) + "/grant", {
    method: "PUT",
    json: { userId: $("grantUser").value, role: $("role").value || null },
  });
  message("프로젝트 권한을 저장했습니다.");
});
action("disableUser", async () => {
  await api("/users/" + encodeURIComponent($("grantUser").value), {
    method: "PUT",
    json: { enabled: false },
  });
  await users();
  message("계정을 비활성화하고 세션을 해제했습니다.");
});
async function jobs() {
  const [j, r] = await Promise.all([api("/jobs"), api("/runs")]);
  $("jobs").replaceChildren();
  for (const job of j.jobs) {
    const li = document.createElement("li");
    li.textContent =
      job.id +
      " · " +
      job.state +
      " · 다음 " +
      new Date(job.next_at * 1000).toLocaleString() +
      " ";
    if (job.enabled) {
      const b = document.createElement("button");
      b.textContent = "예약 취소";
      b.onclick = () =>
        api("/jobs/" + job.id, { method: "DELETE" })
          .then(jobs)
          .catch((e) => message(e.message));
      li.append(b);
    }
    $("jobs").append(li);
  }
  $("runs").textContent = JSON.stringify(r.runs, null, 2);
}
action("jobsRefresh", jobs);
action("schedule", async () => {
  const b = batch();
  const start = new Date($("start").value);
  if (Number.isNaN(start.getTime()))
    throw new Error("첫 실행 시각을 입력하세요.");
  const interval = Number($("interval").value) * 60;
  await api("/jobs", {
    method: "POST",
    json: {
      projectId: current.id,
      intervalSeconds: interval,
      startAt: Math.floor(start.getTime() / 1000),
      ...b,
    },
  });
  await jobs();
  message("예약을 저장했습니다. 서버가 실행 중일 때 주기 실행됩니다.");
});
setInterval(() => {
  if (token && !stopped) api("/me").catch((e) => message(e.message));
}, 300000);
