"use strict"
// Page logic. Talks to Bridge.cs through host.js: { id, cmd, args } -> { id, ok, result | error }.
const $ = (id) => document.getElementById(id)
let state = null
let nextId = 1
const pending = new Map()

host.addEventListener("message", (e) => {
  const m = e.data
  if (m.event === "import") {
    $("import-box").hidden = false
    $("import-bar").style.width = m.data.pm / 10 + "%"
    $("import-text").textContent = (m.data.pm / 10).toFixed(1) + "%  " + m.data.file
    return
  }
  if (m.event === "dragging") { $("drop").hidden = !m.data; return }
  if (m.event === "state") { show(m.data); return }
  if (m.event === "dropped") {
    const d = m.data
    $("import-box").hidden = d.kind !== "iso" && d.kind !== "zip"
    status({ zip: "Looking inside " + d.name + "…", folder: "Game folder set: " + d.name, iso: "Importing " + d.name + "…", isodone: "Game files ready.",
      update: "Title update installed: " + d.name, error: d.name }[d.kind], d.kind === "error")
    return
  }
  const p = pending.get(m.id)
  if (!p) return
  pending.delete(m.id)
  m.ok ? p.resolve(m.result) : p.reject(new Error(m.error))
})
const call = (cmd, args) => new Promise((resolve, reject) => {
  const id = nextId++
  pending.set(id, { resolve, reject })
  host.postMessage({ id, cmd, args })
})

function status(text, bad) { const s = $("status"); s.textContent = text || ""; s.className = "status" + (bad ? " bad" : "") }

function fill(s) {
  $("profile").value = s.profile
  $("language").value = String(s.language)
  $("skipIntro").classList.toggle("on", s.skipIntro)
  $("resolution").value = s.resolution
  $("aspect").value = s.aspect
  $("fullscreen").value = s.fullscreen ? "1" : "0"
  $("msaa").value = String(s.msaa)
  $("upscaler").value = s.upscaler
  $("renderScale").value = s.renderScale
  $("sharpness").value = Math.round(s.sharpness * 100)
  $("fastGeometry").classList.toggle("on", s.fastGeometry)
  sliders()
}
function sliders() {
  $("renderScaleVal").textContent = $("renderScale").value + "%"
  $("sharpnessVal").textContent = $("sharpness").value
  const off = $("upscaler").value === "off"
  $("renderScale").disabled = off
  $("sharpness").disabled = off
}
function collect() {
  return {
    profile: $("profile").value.trim() || "Player",
    language: +$("language").value,
    skipIntro: $("skipIntro").classList.contains("on"),
    resolution: $("resolution").value,
    aspect: $("aspect").value,
    fullscreen: $("fullscreen").value === "1",
    msaa: +$("msaa").value,
    upscaler: $("upscaler").value,
    renderScale: +$("renderScale").value,
    sharpness: +$("sharpness").value / 100,
    fastGeometry: $("fastGeometry").classList.contains("on"),
  }
}

function show(st) {
  const first = state === null
  state = st
  if (first) {
    const sel = $("resolution")
    const list = new Set(st.resolutions)
    list.add(st.settings.resolution)
    sel.innerHTML = [...list].map((r) => `<option value="${r}">${r.replace("x", " x ")}</option>`).join("")
    fill(st.settings)
  }
  const ok = st.exe && st.files
  $("chips").innerHTML =
    chip(st.exe, "Game program", "topspin4.exe missing") +
    chip(st.files, "Game files", "Game files missing") +
    (st.running ? chip(true, "Game running", "") : "")
  $("checklist").innerHTML =
    row(st.exe, "topspin4.exe found next to the launcher", "topspin4.exe not found next to the launcher") +
    row(st.update, "Title update installed", "No title update (add or drop the package)") +
    row(st.files, "Game files: " + st.gameDir, "Game files missing in " + st.gameDir + ". Use Import game.")
  $("launch").disabled = !ok || st.running
  $("shaderInfo").textContent = st.shaders.files
    ? `${st.shaders.files} files, ${(st.shaders.bytes / 1048576).toFixed(1)} MB`
    : "Empty."
  $("shaders-delete").disabled = !st.shaders.files || st.running
  $("paths").innerHTML = `Install: <b>${esc(st.installDir)}</b><br>User data: <b>${esc(st.userDir)}</b><br>Game: <b>${esc(st.gameDir)}</b>`
}
const esc = (t) => t.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]))
const chip = (ok, good, bad) => `<span class="chip ${ok ? "ok" : "bad"}"><span class="dot"></span>${ok ? good : bad}</span>`
const row = (ok, good, bad) => `<div class="check ${ok ? "ok" : "bad"}"><span class="dot"></span>${esc(ok ? good : bad)}</div>`

async function refresh() { try { show(await call("state")) } catch (e) { status(e.message, true) } }

let saveTimer = 0
function changed() {
  sliders()
  clearTimeout(saveTimer)
  saveTimer = setTimeout(() => call("save", collect()).then(show).catch((e) => status(e.message, true)), 250)
}
for (const id of ["profile", "language", "resolution", "aspect", "fullscreen", "msaa", "upscaler", "renderScale", "sharpness"])
  $(id).addEventListener("input", changed)
for (const id of ["skipIntro", "fastGeometry"])
  $(id).addEventListener("click", () => { $(id).classList.toggle("on"); changed() })

$("launch").addEventListener("click", async () => {
  status("Starting…")
  try { await call("launch", { settings: collect() }); status("Game started.") } catch (e) { status(e.message, true) }
  refresh()
})
async function importWith(cmd) {
  for (const id of ["import-iso", "import-folder"]) $(id).disabled = true
  try {
    show(await call(cmd))
    status(state.files ? "Game files ready." : "")
  } catch (e) { status(e.message, true) }
  $("import-box").hidden = true
  for (const id of ["import-iso", "import-folder"]) $(id).disabled = false
  refresh()
}
$("import-iso").addEventListener("click", () => importWith("import.iso"))
$("import-folder").addEventListener("click", () => importWith("import.folder"))
$("import-cancel").addEventListener("click", () => call("import.cancel").catch(() => {}))
$("update-add").addEventListener("click", async () => {
  try { show(await call("update.add")); status("Title update installed.") } catch (e) { status(e.message, true) }
})
// Dropped files are read by the host (DropTarget.cs); here only stop the page from opening them.
for (const t of ["dragover", "drop"]) document.addEventListener(t, (e) => e.preventDefault())
$("shaders-delete").addEventListener("click", async () => {
  if (!confirm("Delete the shader cache? The next start recompiles shaders (a few minutes).")) return
  try { show(await call("shaders.delete")) } catch (e) { alert(e.message) }
})
for (const b of document.querySelectorAll("[data-open]"))
  b.addEventListener("click", () => call("open", b.dataset.open).catch((e) => alert(e.message)))

for (const b of document.querySelectorAll("#tabs button"))
  b.addEventListener("click", () => {
    for (const x of document.querySelectorAll("#tabs button")) x.classList.toggle("on", x === b)
    for (const p of document.querySelectorAll("main.page")) p.hidden = p.id !== "page-" + b.dataset.tab
  })

// hover tips
const tip = $("tip")
document.addEventListener("mouseover", (e) => {
  const el = e.target.closest("[data-tip]")
  if (!el) { tip.hidden = true; return }
  tip.textContent = el.dataset.tip
  tip.hidden = false
  const r = el.getBoundingClientRect()
  tip.style.left = Math.min(r.left, innerWidth - 330) + "px"
  tip.style.top = Math.min(r.bottom + 6, innerHeight - tip.offsetHeight - 6) + "px"
})

refresh()
setInterval(refresh, 4000)
