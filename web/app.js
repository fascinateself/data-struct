/*
 * Archivo: web/app.js
 * Proposito: Renderizador acelerado via ImageData/OffscreenCanvas (60 FPS en 400x400),
 *            minimapa desacoplado, LOD dinamico, arbol N-ario interactivo en linea de tiempo,
 *            herramientas de edicion con transacciones atomicas y biblioteca de plantillas.
 * Declaracion de transparencia academica:
 *   Codigo asistido/generado mediante inteligencia artificial: 100% (plantilla base para revision y adaptacion).
 */

const DIR_LABELS = ["Arriba", "Izquierda", "Abajo", "Derecha"];
const TERRAIN_NAMES = [
    "No habitable (N)",
    "Colonizable (H)",
    "Transito (T)",
    "Objetivo (O)",
    "Fertil / Recurso (R)",
    "Fortificada / Bastion (F)",
    "Pantano (S)",
    "Corriente / Diodo (W)"
];

const STORAGE_SCENARIOS_KEY = "ca_territorial_scenarios_v3";
const STORAGE_BLUEPRINTS_KEY = "ca_territorial_blueprints_v3";

// Cache grafico de alto rendimiento (OffscreenCanvas + ImageData)
const offscreenCanvas = document.createElement("canvas");
const offscreenCtx = offscreenCanvas.getContext("2d");
let offscreenImageData = null;
let lastUploadedVersion = -1;
let lastUploadedW = 0;
let lastUploadedH = 0;

// Estado de simulacion y camara
let isRunning = false;
let animFrameId = null;
let lastFrameTime = 0;
let camZoom = 10;
let camPanX = 20;
let camPanY = 20;

// Interaccion
let activeTool = "brush";
let isPointerDown = false;
let isPanning = false;
let spacePressed = false;
let panStartScreen = { x: 0, y: 0 };
let panStartCam = { x: 0, y: 0 };
let dragStartCell = null;
let lastBrushCell = null;
let hoverCell = null;

// Plantillas (Blueprints)
let blueprints = [];
let selectedBlueprintIdx = 0;
let blueprintRotation = 0;
let capturedRegion = null;

// Telemetria y linea de tiempo
let telemetryHistory = [];
const MAX_HISTORY_POINTS = 120;
let timelineNodePositions = []; // [{ id, x, y, r }]

// Referencias DOM
const canvas = document.getElementById("simCanvas");
const ctx = canvas.getContext("2d");
const minimapCanvas = document.getElementById("minimapCanvas");
const miniCtx = minimapCanvas.getContext("2d");
const telemetryCanvas = document.getElementById("telemetryCanvas");
const telCtx = telemetryCanvas.getContext("2d");
const timelineCanvas = document.getElementById("timelineCanvas");
const timeCtx = timelineCanvas.getContext("2d");

const lblTick = document.getElementById("lblTick");
const lblNations = document.getElementById("lblNations");
const lblActive = document.getElementById("lblActive");
const lblDim = document.getElementById("lblDim");
const lblZoom = document.getElementById("lblZoom");
const lblInspector = document.getElementById("lblInspector");
const btnPlay = document.getElementById("btnPlay");

// =========================================================================
// 1. Utilidades de Color y Sincronizacion con C++
// =========================================================================

function rgbaIntToHex(rgba) {
    const r = rgba & 255;
    const g = (rgba >>> 8) & 255;
    const b = (rgba >>> 16) & 255;
    return "#" + [r, g, b].map(v => v.toString(16).padStart(2, "0")).join("");
}

function hexToRgb(hex) {
    const clean = hex.replace("#", "");
    return {
        r: parseInt(clean.slice(0, 2), 16) || 0,
        g: parseInt(clean.slice(2, 4), 16) || 0,
        b: parseInt(clean.slice(4, 6), 16) || 0
    };
}

function getTeamHexColor(teamId) {
    return rgbaIntToHex(Module._get_team_color_rgba(teamId) >>> 0);
}

// =========================================================================
// 2. Biblioteca de Plantillas (Blueprints)
// =========================================================================

function parseAsciiBlueprint(name, rows, builtin = true) {
    const h = rows.length;
    const w = rows[0].length;
    const cells = [];
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const ch = rows[y][x];
            let terrain = 0, teamId = 0, troopsFP = 0, growth = 64, flowDir = 3;
            if (ch === "H") terrain = 1;
            else if (ch === "T") terrain = 2;
            else if (ch === "O") terrain = 3;
            else if (ch === "R") { terrain = 4; growth = 112; }
            else if (ch === "F") terrain = 5;
            else if (ch === "S") terrain = 6;
            else if (ch === ">") { terrain = 7; flowDir = 3; }
            else if (ch === "<") { terrain = 7; flowDir = 1; }
            else if (ch === "^") { terrain = 7; flowDir = 0; }
            else if (ch === "v") { terrain = 7; flowDir = 2; }
            else if (ch === "A") { terrain = 1; teamId = 1; troopsFP = 1024; }
            else if (ch === "B") { terrain = 1; teamId = 2; troopsFP = 1024; }
            cells.push({ dx: x, dy: y, terrain, teamId, troopsFP, growth, flowDir });
        }
    }
    return { name, width: w, height: h, cells, builtin };
}

function initBlueprints() {
    const defaults = [
        parseAsciiBlueprint("Compuerta XOR (9x9)", [
            "AHHNNNNNN",
            "AHHTTNNNN",
            "AHHNTNNNN",
            "NNNNTNNNN",
            "NNNHTTTON",
            "NNNNTNNNN",
            "BHHNTNNNN",
            "BHHTTNNNN",
            "BHHNNNNNN"
        ]),
        parseAsciiBlueprint("Reloj con Diodo (6x3)", [
            "AHHNNN",
            "AHHT>>",
            "AHHNNN"
        ]),
        parseAsciiBlueprint("Cruce Ortogonal Lineal (5x5)", [
            "NNTNN",
            "NNTNN",
            "TTTTT",
            "NNTNN",
            "NNTNN"
        ]),
        parseAsciiBlueprint("Bastion Fertil (5x5)", [
            "FFFFF",
            "FRRRF",
            "FRRRF",
            "FRRRF",
            "FFFFF"
        ])
    ];

    let custom = [];
    try {
        const raw = localStorage.getItem(STORAGE_BLUEPRINTS_KEY);
        if (raw) custom = JSON.parse(raw);
    } catch (_) {}

    blueprints = [...defaults, ...custom];
    renderBlueprintList();
}

function saveCustomBlueprints() {
    localStorage.setItem(STORAGE_BLUEPRINTS_KEY, JSON.stringify(blueprints.filter(bp => !bp.builtin)));
}

function rotateDirClockwise(dir, rot) {
    // 0:UP -> 3:RIGHT -> 2:DOWN -> 1:LEFT -> 0:UP
    const cw = [3, 0, 1, 2];
    let d = dir & 3;
    for (let i = 0; i < rot; i++) d = cw[d];
    return d;
}

function getRotatedBlueprintCells(bp, rot) {
    const bw = bp.width;
    const bh = bp.height;
    const outW = (rot % 2 === 0) ? bw : bh;
    const outH = (rot % 2 === 0) ? bh : bw;
    const cells = bp.cells.map(c => {
        let rx = c.dx, ry = c.dy;
        if (rot === 1) { rx = bh - 1 - c.dy; ry = c.dx; }
        else if (rot === 2) { rx = bw - 1 - c.dx; ry = bh - 1 - c.dy; }
        else if (rot === 3) { rx = c.dy; ry = bw - 1 - c.dx; }
        return {
            dx: rx,
            dy: ry,
            terrain: c.terrain,
            teamId: c.teamId,
            troopsFP: c.troopsFP || 1024,
            growth: c.growth || 64,
            flowDir: rotateDirClockwise(c.flowDir || 0, rot)
        };
    });
    return { width: outW, height: outH, cells };
}

function renderBlueprintList() {
    const container = document.getElementById("blueprintList");
    container.innerHTML = "";
    blueprints.forEach((bp, idx) => {
        const div = document.createElement("div");
        div.className = "blueprint-item" + (idx === selectedBlueprintIdx ? " selected" : "");
        const span = document.createElement("span");
        span.textContent = `${bp.name} (${bp.width}x${bp.height})`;
        div.appendChild(span);

        if (!bp.builtin) {
            const delBtn = document.createElement("button");
            delBtn.textContent = "X";
            delBtn.className = "danger";
            delBtn.style.padding = "0.12rem 0.35rem";
            delBtn.onclick = (e) => {
                e.stopPropagation();
                blueprints.splice(idx, 1);
                if (selectedBlueprintIdx >= blueprints.length) selectedBlueprintIdx = 0;
                saveCustomBlueprints();
                renderBlueprintList();
            };
            div.appendChild(delBtn);
        }

        div.onclick = () => {
            selectedBlueprintIdx = idx;
            setActiveTool("stamp");
            renderBlueprintList();
            renderAll();
        };
        container.appendChild(div);
    });
}

// =========================================================================
// 3. Subida de Textura a GPU (ImageData) y Renderizado a 60 FPS + LOD
// =========================================================================

function syncOffscreenTextureIfNeeded() {
    const w = Module._get_width();
    const h = Module._get_height();
    const ver = Module._get_render_version() >>> 0;

    if (w <= 0 || h <= 0) return false;

    if (w !== lastUploadedW || h !== lastUploadedH) {
        offscreenCanvas.width = w;
        offscreenCanvas.height = h;
        offscreenImageData = offscreenCtx.createImageData(w, h);
        lastUploadedW = w;
        lastUploadedH = h;
        lastUploadedVersion = -1;
    }

    if (ver !== lastUploadedVersion) {
        const ptr = Module._get_pixel_buffer();
        const bytes = new Uint8ClampedArray(Module.HEAPU8.buffer, ptr, w * h * 4);
        offscreenImageData.data.set(bytes);
        offscreenCtx.putImageData(offscreenImageData, 0, 0);
        lastUploadedVersion = ver;
        return true;
    }
    return false;
}

function resizeCanvas() {
    const stage = canvas.parentElement;
    canvas.width = stage.clientWidth;
    canvas.height = stage.clientHeight;
    renderAll();
}

function resetCamera() {
    const w = Module._get_width();
    const h = Module._get_height();
    if (w <= 0 || h <= 0) return;
    const pad = 20;
    camZoom = Math.max(1, Math.min((canvas.width - pad * 2) / w, (canvas.height - pad * 2) / h));
    camPanX = (canvas.width - w * camZoom) / 2;
    camPanY = (canvas.height - h * camZoom) / 2;
    lblZoom.textContent = `${Math.round(camZoom * 10)}%`;
}

function screenToCell(sx, sy) {
    return {
        x: Math.floor((sx - camPanX) / camZoom),
        y: Math.floor((sy - camPanY) / camZoom)
    };
}

function computeLinePoints(x0, y0, x1, y1) {
    const pts = [];
    let dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0);
    let sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    let cx = x0, cy = y0;
    while (true) {
        pts.push({ x: cx, y: cy });
        if (cx === x1 && cy === y1) break;
        const e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
    return pts;
}

function renderAll() {
    if (!Module._get_width) return;
    const w = Module._get_width();
    const h = Module._get_height();
    if (w <= 0 || h <= 0) return;

    syncOffscreenTextureIfNeeded();

    lblTick.textContent = Module._get_tick();
    lblNations.textContent = Module._get_nation_count();
    lblActive.textContent = Module._get_active_cell_count();
    lblDim.textContent = `${w}x${h}`;

    // Fondo del lienzo
    ctx.fillStyle = "#060a14";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    // Proyeccion acelerada en una sola llamada GPU (soporta 400x400 a 60 FPS)
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(offscreenCanvas, camPanX, camPanY, w * camZoom, h * camZoom);

    // Nivel de Detalle Dinamico (LOD): solo cuando el zoom muestra celdas grandes
    const showDetails = document.getElementById("chkShowGrid").checked && camZoom >= 14;
    if (showDetails) {
        renderHighZoomLOD(w, h);
    }

    ctx.strokeStyle = "#475569";
    ctx.lineWidth = 1.5;
    ctx.strokeRect(camPanX, camPanY, w * camZoom, h * camZoom);

    if (document.getElementById("chkGraphOverlay").checked) {
        renderGraphOverlay();
    }

    renderToolOverlay(w, h);
    renderMinimap(w, h);
}

function renderHighZoomLOD(w, h) {
    const minX = Math.max(0, Math.floor(-camPanX / camZoom));
    const maxX = Math.min(w - 1, Math.ceil((canvas.width - camPanX) / camZoom));
    const minY = Math.max(0, Math.floor(-camPanY / camZoom));
    const maxY = Math.min(h - 1, Math.ceil((canvas.height - camPanY) / camZoom));

    const metaPtr = Module._get_meta_buffer();
    const meta = new Uint16Array(Module.HEAPU16.buffer, metaPtr, w * h * 4);

    ctx.lineWidth = 1;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";

    for (let y = minY; y <= maxY; y++) {
        const py = camPanY + y * camZoom;
        for (let x = minX; x <= maxX; x++) {
            const px = camPanX + x * camZoom;
            const base = (y * w + x) * 4;
            const rawT = meta[base + 0];
            const terrain = rawT & 15;
            const flowDir = (rawT >>> 4) & 3;
            const team = meta[base + 1];
            const flags = meta[base + 2];
            const val = meta[base + 3];

            // Rejilla sutil
            ctx.strokeStyle = "rgba(15, 23, 42, 0.22)";
            ctx.strokeRect(px, py, camZoom, camZoom);

            if (terrain === 2) {
                // Tramado diagonal en T
                ctx.strokeStyle = "rgba(180, 83, 9, 0.35)";
                ctx.beginPath();
                ctx.moveTo(px, py + camZoom);
                ctx.lineTo(px + camZoom, py);
                ctx.stroke();
            } else if (terrain === 7) {
                // Flecha de direccion en Corriente / Diodo W
                const arrows = ["^", "<", "v", ">"];
                ctx.fillStyle = team > 0 ? "#ffffff" : "#0369a1";
                ctx.font = `bold ${Math.floor(camZoom * 0.5)}px monospace`;
                ctx.fillText(arrows[flowDir], px + camZoom / 2, py + camZoom / 2);
            } else if (terrain === 3) {
                ctx.strokeStyle = "#059669";
                ctx.lineWidth = 2;
                ctx.strokeRect(px + 1, py + 1, camZoom - 2, camZoom - 2);
                ctx.lineWidth = 1;
                if (team === 0 && camZoom >= 20) {
                    ctx.fillStyle = "#065f46";
                    ctx.font = `bold ${Math.floor(camZoom * 0.45)}px sans-serif`;
                    ctx.fillText("O", px + camZoom / 2, py + camZoom / 2);
                }
            } else if (terrain === 5) {
                // Borde de Bastion F
                ctx.strokeStyle = "rgba(248, 250, 252, 0.55)";
                ctx.strokeRect(px + 2, py + 2, camZoom - 4, camZoom - 4);
            }

            // Indicadores de multiples canales activos en T o W
            if ((flags & 1) !== 0) {
                ctx.strokeStyle = "#ffffff";
                ctx.strokeRect(px + 2, py + 2, camZoom - 4, camZoom - 4);
                ctx.fillStyle = "#ffffff";
                const dot = Math.max(2, camZoom * 0.12);
                if (flags & 2) ctx.fillRect(px + camZoom / 2 - dot / 2, py + 2, dot, dot);
                if (flags & 4) ctx.fillRect(px + 2, py + camZoom / 2 - dot / 2, dot, dot);
                if (flags & 8) ctx.fillRect(px + camZoom / 2 - dot / 2, py + camZoom - dot - 2, dot, dot);
                if (flags & 16) ctx.fillRect(px + camZoom - dot - 2, py + camZoom / 2 - dot / 2, dot, dot);
            }

            if (camZoom >= 24 && team > 0 && val > 0 && terrain !== 7) {
                ctx.fillStyle = "#ffffff";
                ctx.font = `bold ${Math.floor(camZoom * 0.34)}px monospace`;
                ctx.fillText(String(val), px + camZoom / 2, py + camZoom / 2);
            }
        }
    }
}

function renderMinimap(w, h) {
    const mw = minimapCanvas.width;
    const mh = minimapCanvas.height;
    miniCtx.imageSmoothingEnabled = false;
    miniCtx.fillStyle = "#0b1120";
    miniCtx.fillRect(0, 0, mw, mh);

    // Una sola llamada GPU usando el offscreenCanvas cacheado (0 ms al mover la camara)
    miniCtx.drawImage(offscreenCanvas, 0, 0, mw, mh);

    const sx = mw / w;
    const sy = mh / h;
    const vx = (-camPanX / camZoom) * sx;
    const vy = (-camPanY / camZoom) * sy;
    const vw = (canvas.width / camZoom) * sx;
    const vh = (canvas.height / camZoom) * sy;
    miniCtx.strokeStyle = "#38bdf8";
    miniCtx.lineWidth = 1.5;
    miniCtx.strokeRect(vx, vy, vw, vh);
}

function renderGraphOverlay() {
    const nodeCount = Module._get_graph_node_count();
    const edgeCount = Module._get_graph_edge_count();
    const nodesPtr = Module._get_graph_nodes_ptr();
    const edgesPtr = Module._get_graph_edges_ptr();

    if (edgeCount > 0) {
        const edges = new Float32Array(Module.HEAPF32.buffer, edgesPtr, edgeCount * 4);
        ctx.strokeStyle = "rgba(250, 204, 21, 0.85)";
        ctx.lineWidth = 2;
        ctx.beginPath();
        for (let i = 0; i < edgeCount; i++) {
            ctx.moveTo(camPanX + edges[i * 4 + 0] * camZoom, camPanY + edges[i * 4 + 1] * camZoom);
            ctx.lineTo(camPanX + edges[i * 4 + 2] * camZoom, camPanY + edges[i * 4 + 3] * camZoom);
        }
        ctx.stroke();
    }

    if (nodeCount > 0) {
        const nodes = new Float32Array(Module.HEAPF32.buffer, nodesPtr, nodeCount * 5);
        for (let i = 0; i < nodeCount; i++) {
            const cx = camPanX + nodes[i * 5 + 0] * camZoom;
            const cy = camPanY + nodes[i * 5 + 1] * camZoom;
            const teamId = Math.round(nodes[i * 5 + 2]);
            const area = Math.round(nodes[i * 5 + 3]);
            const r = Math.max(5, Math.min(18, Math.sqrt(area) * 1.5));

            ctx.fillStyle = getTeamHexColor(teamId);
            ctx.strokeStyle = "#f8fafc";
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.fill();
            ctx.stroke();

            ctx.fillStyle = "#ffffff";
            ctx.font = "bold 10px monospace";
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText(String(area), cx, cy);
        }
    }
}

function renderToolOverlay(w, h) {
    if (activeTool === "stamp" && hoverCell && blueprints[selectedBlueprintIdx]) {
        const rotBp = getRotatedBlueprintCells(blueprints[selectedBlueprintIdx], blueprintRotation);
        ctx.save();
        ctx.globalAlpha = 0.65;
        for (const c of rotBp.cells) {
            const gx = hoverCell.x + c.dx;
            const gy = hoverCell.y + c.dy;
            if (gx < 0 || gx >= w || gy < 0 || gy >= h) continue;
            const px = camPanX + gx * camZoom;
            const py = camPanY + gy * camZoom;
            if (c.teamId > 0) ctx.fillStyle = getTeamHexColor(c.teamId);
            else if (c.terrain === 0) ctx.fillStyle = "#182234";
            else if (c.terrain === 1) ctx.fillStyle = "#e2e8f0";
            else if (c.terrain === 2) ctx.fillStyle = "#fde68a";
            else if (c.terrain === 3) ctx.fillStyle = "#34d399";
            else if (c.terrain === 4) ctx.fillStyle = "#86efac";
            else if (c.terrain === 5) ctx.fillStyle = "#94a3b8";
            else if (c.terrain === 6) ctx.fillStyle = "#a3b18a";
            else ctx.fillStyle = "#7dd3fc";
            ctx.fillRect(px, py, camZoom, camZoom);
        }
        ctx.globalAlpha = 1.0;
        ctx.strokeStyle = "#38bdf8";
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 3]);
        ctx.strokeRect(
            camPanX + hoverCell.x * camZoom,
            camPanY + hoverCell.y * camZoom,
            rotBp.width * camZoom,
            rotBp.height * camZoom
        );
        ctx.restore();
    }

    if (isPointerDown && dragStartCell && hoverCell) {
        const filled = document.getElementById("chkShapeFilled").checked;
        ctx.save();
        ctx.fillStyle = "rgba(56, 189, 248, 0.35)";
        ctx.strokeStyle = "#38bdf8";
        ctx.lineWidth = 2;

        if (activeTool === "line") {
            for (const p of computeLinePoints(dragStartCell.x, dragStartCell.y, hoverCell.x, hoverCell.y)) {
                ctx.fillRect(camPanX + p.x * camZoom, camPanY + p.y * camZoom, camZoom, camZoom);
            }
        } else if (activeTool === "rect" || activeTool === "capture") {
            const minX = Math.min(dragStartCell.x, hoverCell.x);
            const maxX = Math.max(dragStartCell.x, hoverCell.x);
            const minY = Math.min(dragStartCell.y, hoverCell.y);
            const maxY = Math.max(dragStartCell.y, hoverCell.y);
            const rx = camPanX + minX * camZoom;
            const ry = camPanY + minY * camZoom;
            const rw = (maxX - minX + 1) * camZoom;
            const rh = (maxY - minY + 1) * camZoom;
            if (filled || activeTool === "capture") ctx.fillRect(rx, ry, rw, rh);
            ctx.strokeRect(rx, ry, rw, rh);
        } else if (activeTool === "circle") {
            const r = Math.round(Math.hypot(hoverCell.x - dragStartCell.x, hoverCell.y - dragStartCell.y));
            const r2 = r * r;
            const ir2 = r > 0 ? (r - 1) * (r - 1) : 0;
            for (let dy = -r; dy <= r; dy++) {
                for (let dx = -r; dx <= r; dx++) {
                    const d2 = dx * dx + dy * dy;
                    if (d2 <= r2 && (filled || r === 0 || d2 >= ir2)) {
                        const gx = dragStartCell.x + dx;
                        const gy = dragStartCell.y + dy;
                        if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
                            ctx.fillRect(camPanX + gx * camZoom, camPanY + gy * camZoom, camZoom, camZoom);
                        }
                    }
                }
            }
        }
        ctx.restore();
    }
}

// =========================================================================
// 4. Visualizacion Interactiva del Arbol N-ario de Historial
// =========================================================================

function renderTimelineTree() {
    const count = Module._get_history_node_count();
    document.getElementById("lblTreeNodes").textContent = `Nodos: ${count}`;
    if (count <= 0) return;

    const ptr = Module._get_history_tree_ptr();
    const raw = new Int32Array(Module.HEAP32.buffer, ptr, count * 5);

    const nodes = [];
    const childrenMap = {};
    for (let i = 0; i < count; i++) {
        const id = raw[i * 5 + 0];
        const parentId = raw[i * 5 + 1];
        const tick = raw[i * 5 + 2];
        const kind = raw[i * 5 + 3];
        const isCurrent = raw[i * 5 + 4] === 1;
        nodes.push({ id, parentId, tick, kind, isCurrent, depth: 0, lane: 0 });
        if (!childrenMap[parentId]) childrenMap[parentId] = [];
        childrenMap[parentId].push(id);
    }

    // Asignacion determinista de profundidad (X) y carril de rama (Y)
    let nextLane = 0;
    let maxDepth = 0;
    function layoutSubtree(id, depth, lane) {
        const node = nodes[id];
        node.depth = depth;
        node.lane = lane;
        if (depth > maxDepth) maxDepth = depth;
        const kids = childrenMap[id] || [];
        kids.forEach((childId, idx) => {
            const childLane = idx === 0 ? lane : ++nextLane;
            layoutSubtree(childId, depth + 1, childLane);
        });
    }
    layoutSubtree(0, 0, 0);

    const stepX = 36;
    const stepY = 20;
    const padX = 20;
    const padY = 18;
    timelineCanvas.width = Math.max(timelineCanvas.parentElement.clientWidth, (maxDepth + 2) * stepX + padX);
    timelineCanvas.height = Math.max(60, (nextLane + 1) * stepY + padY * 2);

    timeCtx.fillStyle = "#0f172a";
    timeCtx.fillRect(0, 0, timelineCanvas.width, timelineCanvas.height);

    // Aristas padre -> hijo
    timeCtx.strokeStyle = "#475569";
    timeCtx.lineWidth = 1.5;
    for (const n of nodes) {
        if (n.parentId < 0) continue;
        const p = nodes[n.parentId];
        const x1 = padX + p.depth * stepX;
        const y1 = padY + p.lane * stepY;
        const x2 = padX + n.depth * stepX;
        const y2 = padY + n.lane * stepY;
        timeCtx.beginPath();
        timeCtx.moveTo(x1, y1);
        timeCtx.bezierCurveTo(x1 + stepX * 0.5, y1, x2 - stepX * 0.5, y2, x2, y2);
        timeCtx.stroke();
    }

    // Nodos interactivos
    timelineNodePositions = [];
    for (const n of nodes) {
        const x = padX + n.depth * stepX;
        const y = padY + n.lane * stepY;
        const r = n.isCurrent ? 7 : 5;
        timelineNodePositions.push({ id: n.id, x, y, r: 10 });

        let fill = "#64748b";
        if (n.kind === 1) fill = "#3b82f6"; // Simulacion
        else if (n.kind === 2) fill = "#f59e0b"; // Edicion
        if (n.isCurrent) fill = "#10b981";

        timeCtx.fillStyle = fill;
        timeCtx.beginPath();
        timeCtx.arc(x, y, r, 0, Math.PI * 2);
        timeCtx.fill();

        if (n.isCurrent) {
            timeCtx.strokeStyle = "#ffffff";
            timeCtx.lineWidth = 2;
            timeCtx.stroke();
            timelineCanvas.parentElement.scrollLeft = Math.max(0, x - timelineCanvas.parentElement.clientWidth + 80);
        }

        timeCtx.fillStyle = "#cbd5e1";
        timeCtx.font = "9px monospace";
        timeCtx.textAlign = "center";
        timeCtx.fillText(`T${n.tick}`, x, y + 14);
    }
}

timelineCanvas.addEventListener("click", (evt) => {
    const rect = timelineCanvas.getBoundingClientRect();
    const mx = evt.clientX - rect.left;
    const my = evt.clientY - rect.top;
    for (const np of timelineNodePositions) {
        if (Math.hypot(mx - np.x, my - np.y) <= np.r) {
            stopSimulation();
            if (Module._jump_to_history_node(np.id) === 1) {
                refreshTeamSelectors();
                recordTelemetryPoint();
                renderTimelineTree();
                renderAll();
            }
            break;
        }
    }
});

// =========================================================================
// 5. Equipos, Telemetria y Serializacion (RLE con growthFactor y flowDir)
// =========================================================================

function refreshTeamSelectors() {
    const count = Module._get_team_count();
    const selBrush = document.getElementById("selActiveTeamBrush");
    const selCfg = document.getElementById("selConfigTeam");
    const prevBrush = selBrush.value;
    const prevCfg = selCfg.value;

    selBrush.innerHTML = "";
    selCfg.innerHTML = "";
    for (let t = 1; t <= count; t++) {
        if (Module._is_team_active(t) === 1) {
            const o1 = document.createElement("option");
            o1.value = t; o1.textContent = `Equipo ${t}`;
            selBrush.appendChild(o1);
            const o2 = document.createElement("option");
            o2.value = t; o2.textContent = `Equipo ${t}`;
            selCfg.appendChild(o2);
        }
    }
    if (prevBrush && selBrush.querySelector(`option[value="${prevBrush}"]`)) selBrush.value = prevBrush;
    if (prevCfg && selCfg.querySelector(`option[value="${prevCfg}"]`)) selCfg.value = prevCfg;
    loadTeamConfigToUI();
}

function loadTeamConfigToUI() {
    const t = Number(document.getElementById("selConfigTeam").value);
    if (!t) return;
    document.getElementById("inpTeamColor").value = getTeamHexColor(t);
    document.getElementById("cfgDir0").value = Math.round(Module._get_team_config_param(t, 0));
    document.getElementById("cfgDir1").value = Math.round(Module._get_team_config_param(t, 1));
    document.getElementById("cfgDir2").value = Math.round(Module._get_team_config_param(t, 2));
    document.getElementById("cfgDir3").value = Math.round(Module._get_team_config_param(t, 3));
    document.getElementById("cfgTau").value = Number(Module._get_team_config_param(t, 4)).toFixed(3);
    document.getElementById("cfgPeriod").value = Math.round(Module._get_team_config_param(t, 5));
    document.getElementById("cfgMaxArea").value = Math.round(Module._get_team_config_param(t, 6));
    document.getElementById("cfgFriction").value = Number(Module._get_team_config_param(t, 7)).toFixed(4);
    document.getElementById("cfgMultiDir").checked = Module._get_team_config_param(t, 8) >= 0.5;
    document.getElementById("cfgRegen").value = Number(Module._get_team_config_param(t, 9)).toFixed(2);
    document.getElementById("cfgMaxTroops").value = Number(Module._get_team_config_param(t, 10)).toFixed(1);
    document.getElementById("cfgOrganic").checked = Module._get_team_config_param(t, 11) >= 0.5;
}

function serializeCurrentWorld() {
    const w = Module._get_width();
    const h = Module._get_height();
    const teamCount = Module._get_team_count();
    const teams = [];

    for (let t = 1; t <= teamCount; t++) {
        if (Module._is_team_active(t) === 1) {
            teams.push({
                id: t,
                color: getTeamHexColor(t),
                order: [
                    Math.round(Module._get_team_config_param(t, 0)),
                    Math.round(Module._get_team_config_param(t, 1)),
                    Math.round(Module._get_team_config_param(t, 2)),
                    Math.round(Module._get_team_config_param(t, 3))
                ],
                tau: Module._get_team_config_param(t, 4),
                emissionPeriod: Math.round(Module._get_team_config_param(t, 5)),
                maxArea: Math.round(Module._get_team_config_param(t, 6)),
                friction: Module._get_team_config_param(t, 7),
                multiDir: Module._get_team_config_param(t, 8) >= 0.5,
                regenRate: Module._get_team_config_param(t, 9),
                maxTroops: Module._get_team_config_param(t, 10),
                organic: Module._get_team_config_param(t, 11) >= 0.5
            });
        }
    }

    const rle = [];
    let lastKey = null, runCount = 0, lastTuple = null;
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const terrain = Module._get_cell_terrain(x, y);
            const team = Module._get_cell_team(x, y);
            const troopsFP = Module._get_cell_troops_fp(x, y);
            const growth = Module._get_cell_growth_factor(x, y);
            const flowDir = Module._get_cell_flow_dir(x, y);
            const key = `${terrain}|${team}|${troopsFP}|${growth}|${flowDir}`;
            if (key === lastKey) {
                runCount++;
            } else {
                if (lastTuple) rle.push([runCount, ...lastTuple]);
                lastKey = key;
                lastTuple = [terrain, team, troopsFP, growth, flowDir];
                runCount = 1;
            }
        }
    }
    if (lastTuple) rle.push([runCount, ...lastTuple]);
    return { version: 3, width: w, height: h, teams, rle };
}

function loadSerializedWorld(data) {
    stopSimulation();
    Module._init_blank_map(data.width, data.height);

    if (Array.isArray(data.teams)) {
        for (const tc of data.teams) {
            const rgb = hexToRgb(tc.color || "#3b82f6");
            Module._set_team_color(tc.id, rgb.r, rgb.g, rgb.b);
            Module._set_team_config(
                tc.id, tc.order[0], tc.order[1], tc.order[2], tc.order[3],
                tc.tau, tc.emissionPeriod, tc.maxArea, tc.friction,
                tc.multiDir ? 1 : 0, tc.regenRate, tc.maxTroops,
                tc.organic !== false ? 1 : 0
            );
        }
    }

    let cellIdx = 0;
    const w = data.width;
    for (const run of data.rle) {
        const count = run[0];
        const terrain = run[1];
        const team = run[2];
        const troopsFP = data.version >= 3 ? run[3] : Math.round((run[3] || 1) * 1024);
        const growth = run.length > 4 ? run[4] : 64;
        const flowDir = run.length > 5 ? run[5] : 3;
        for (let i = 0; i < count; i++) {
            Module._set_cell_exact(cellIdx % w, Math.floor(cellIdx / w), terrain, team, troopsFP, growth, flowDir);
            cellIdx++;
        }
    }

    Module._commit_edit_transaction();
    telemetryHistory = [];
    refreshTeamSelectors();
    resetCamera();
    recordTelemetryPoint();
    renderTimelineTree();
    renderAll();
}

function getSavedScenariosMap() {
    try { return JSON.parse(localStorage.getItem(STORAGE_SCENARIOS_KEY) || "{}"); }
    catch (_) { return {}; }
}

function refreshScenarioDropdown() {
    const sel = document.getElementById("selScenario");
    const prev = sel.value;
    sel.innerHTML = `
        <option value="__sandbox">Sandbox Organico (80x60)</option>
        <option value="__sandbox_large">Prueba de Rendimiento (250x250)</option>
        <option value="__sandbox_xl">Prueba Extrema (400x400)</option>
        <option value="__xor_10">Compuerta XOR (A=1, B=0)</option>
        <option value="__xor_01">Compuerta XOR (A=0, B=1)</option>
        <option value="__xor_11">Compuerta XOR (A=1, B=1)</option>
        <option value="__xor_00">Compuerta XOR (A=0, B=0)</option>
    `;
    const saved = getSavedScenariosMap();
    for (const name of Object.keys(saved)) {
        const opt = document.createElement("option");
        opt.value = "custom:" + name;
        opt.textContent = "[Guardado] " + name;
        sel.appendChild(opt);
    }
    if (prev && sel.querySelector(`option[value="${prev}"]`)) sel.value = prev;
}

function recordTelemetryPoint() {
    const count = Module._get_team_count();
    const areas = {};
    for (let t = 1; t <= count; t++) {
        if (Module._is_team_active(t) === 1) areas[t] = Module._get_team_area(t);
    }
    telemetryHistory.push({ tick: Module._get_tick(), areas });
    if (telemetryHistory.length > MAX_HISTORY_POINTS) telemetryHistory.shift();
    renderTelemetry();
}

function renderTelemetry() {
    const tbody = document.getElementById("telemetryTableBody");
    tbody.innerHTML = "";
    const count = Module._get_team_count();
    const activeTeams = [];

    for (let t = 1; t <= count; t++) {
        if (Module._is_team_active(t) === 1) {
            activeTeams.push(t);
            const tr = document.createElement("tr");
            tr.innerHTML = `
                <td><span class="swatch" style="background:${getTeamHexColor(t)}"></span>Eq. ${t}</td>
                <td>${Module._get_team_nation_count(t)}</td>
                <td>${Module._get_team_area(t)}</td>
                <td>${Module._get_team_troops(t).toFixed(1)}</td>
            `;
            tbody.appendChild(tr);
        }
    }

    const cw = telemetryCanvas.width, ch = telemetryCanvas.height;
    telCtx.fillStyle = "#0b1120";
    telCtx.fillRect(0, 0, cw, ch);
    if (telemetryHistory.length < 2) return;

    let maxArea = 10;
    for (const pt of telemetryHistory) {
        for (const t of activeTeams) {
            if ((pt.areas[t] || 0) > maxArea) maxArea = pt.areas[t];
        }
    }
    const pad = 8;
    const stepX = (cw - pad * 2) / (telemetryHistory.length - 1);
    for (const t of activeTeams) {
        telCtx.strokeStyle = getTeamHexColor(t);
        telCtx.lineWidth = 2;
        telCtx.beginPath();
        telemetryHistory.forEach((pt, idx) => {
            const x = pad + idx * stepX;
            const y = ch - pad - ((pt.areas[t] || 0) / maxArea) * (ch - pad * 2);
            if (idx === 0) telCtx.moveTo(x, y); else telCtx.lineTo(x, y);
        });
        telCtx.stroke();
    }
}

// =========================================================================
// 6. Bucle de Simulacion y Eventos de Entrada
// =========================================================================

function simulationLoop(timestamp) {
    if (!isRunning) return;
    const fps = Number(document.getElementById("rngFps").value);
    if (timestamp - lastFrameTime >= 1000 / fps) {
        lastFrameTime = timestamp;
        const batch = Number(document.getElementById("rngBatch").value);
        const recUndo = document.getElementById("chkTickUndo").checked ? 1 : 0;
        Module._step_batch(batch, recUndo);
        recordTelemetryPoint();
        if (recUndo) renderTimelineTree();
        renderAll();
    }
    animFrameId = requestAnimationFrame(simulationLoop);
}

function startSimulation() {
    if (isRunning) return;
    isRunning = true;
    btnPlay.textContent = "Pausar";
    lastFrameTime = performance.now();
    animFrameId = requestAnimationFrame(simulationLoop);
}

function stopSimulation() {
    isRunning = false;
    btnPlay.textContent = "Iniciar";
    if (animFrameId !== null) {
        cancelAnimationFrame(animFrameId);
        animFrameId = null;
    }
}

function setActiveTool(tool) {
    activeTool = tool;
    document.querySelectorAll(".tool-btn").forEach(b => b.classList.toggle("active-tool", b.dataset.tool === tool));
    document.getElementById("shapeFillRow").style.display = (tool === "rect" || tool === "circle") ? "flex" : "none";
    document.getElementById("dispatchControls").style.display = (tool === "dispatch") ? "block" : "none";
    canvas.style.cursor = (tool === "pan") ? "grab" : "crosshair";
}

function getMaterialAuxParam() {
    const mode = Number(document.getElementById("selMaterial").value);
    if (mode === 9) return Number(document.getElementById("selFlowDir").value);
    if (mode === 10) return Number(document.getElementById("rngGrowthBrush").value);
    return 0;
}

function applyBrushPoint(x, y) {
    const mode = Number(document.getElementById("selMaterial").value);
    const teamId = Number(document.getElementById("selActiveTeamBrush").value) || 1;
    Module._paint_cell_raw(x, y, mode, teamId, getMaterialAuxParam());
}

canvas.addEventListener("mousedown", (evt) => {
    const rect = canvas.getBoundingClientRect();
    const sx = evt.clientX - rect.left;
    const sy = evt.clientY - rect.top;

    if (evt.button === 1 || spacePressed || activeTool === "pan") {
        isPanning = true;
        panStartScreen = { x: sx, y: sy };
        panStartCam = { x: camPanX, y: camPanY };
        canvas.style.cursor = "grabbing";
        return;
    }
    if (evt.button !== 0) return;

    const cell = screenToCell(sx, sy);
    const w = Module._get_width(), h = Module._get_height();
    if (cell.x < 0 || cell.x >= w || cell.y < 0 || cell.y >= h) return;

    isPointerDown = true;
    dragStartCell = cell;
    hoverCell = cell;

    if (activeTool === "brush") {
        Module._begin_edit_transaction();
        applyBrushPoint(cell.x, cell.y);
        lastBrushCell = cell;
        renderAll();
    } else if (activeTool === "fill") {
        Module._begin_edit_transaction();
        const mode = Number(document.getElementById("selMaterial").value);
        const teamId = Number(document.getElementById("selActiveTeamBrush").value) || 1;
        Module._flood_fill(cell.x, cell.y, mode, teamId, getMaterialAuxParam());
        Module._commit_edit_transaction();
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    } else if (activeTool === "eraseNation") {
        Module._begin_edit_transaction();
        Module._erase_nation_at(cell.x, cell.y);
        Module._commit_edit_transaction();
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    } else if (activeTool === "dispatch") {
        const dir = Number(document.getElementById("selDispatchDir").value);
        const frac = Number(document.getElementById("selDispatchFrac").value);
        Module._dispatch_troops(cell.x, cell.y, dir, frac);
        if (!isRunning) {
            Module._step_batch(1, 1);
            recordTelemetryPoint();
            renderTimelineTree();
        }
        renderAll();
    } else if (activeTool === "stamp" && blueprints[selectedBlueprintIdx]) {
        Module._begin_edit_transaction();
        const rotBp = getRotatedBlueprintCells(blueprints[selectedBlueprintIdx], blueprintRotation);
        for (const c of rotBp.cells) {
            Module._set_cell_exact(cell.x + c.dx, cell.y + c.dy, c.terrain, c.teamId, c.troopsFP, c.growth, c.flowDir);
        }
        Module._commit_edit_transaction();
        refreshTeamSelectors();
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    }
});

canvas.addEventListener("mousemove", (evt) => {
    const rect = canvas.getBoundingClientRect();
    const sx = evt.clientX - rect.left;
    const sy = evt.clientY - rect.top;

    if (isPanning) {
        camPanX = panStartCam.x + (sx - panStartScreen.x);
        camPanY = panStartCam.y + (sy - panStartScreen.y);
        renderAll();
        return;
    }

    const cell = screenToCell(sx, sy);
    hoverCell = cell;
    const w = Module._get_width(), h = Module._get_height();

    if (cell.x >= 0 && cell.x < w && cell.y >= 0 && cell.y < h) {
        const terrain = Module._get_cell_terrain(cell.x, cell.y);
        const team = Module._get_cell_team(cell.x, cell.y);
        const nId = Module._get_cell_nation_id(cell.x, cell.y);
        const nSize = Module._get_cell_nation_size(cell.x, cell.y);
        const troops = Module._get_cell_troops_fp(cell.x, cell.y) / 1024;
        const gf = Module._get_cell_growth_factor(cell.x, cell.y);
        lblInspector.textContent =
            `(${cell.x}, ${cell.y}) | ${TERRAIN_NAMES[terrain]} | Fertilidad: ${(gf / 64).toFixed(2)}x (${gf}) | ` +
            `Eq: ${team || "-"} | Nacion #${nId} (${nSize}) | Tropas: ${troops.toFixed(1)}`;
    }

    if (isPointerDown && activeTool === "brush") {
        if (lastBrushCell && (cell.x !== lastBrushCell.x || cell.y !== lastBrushCell.y)) {
            for (const p of computeLinePoints(lastBrushCell.x, lastBrushCell.y, cell.x, cell.y)) {
                applyBrushPoint(p.x, p.y);
            }
            lastBrushCell = cell;
            renderAll();
        }
    } else if (activeTool === "stamp" || (isPointerDown && (activeTool === "line" || activeTool === "rect" || activeTool === "circle" || activeTool === "capture"))) {
        renderAll();
    }
});

window.addEventListener("mouseup", () => {
    if (isPanning) {
        isPanning = false;
        canvas.style.cursor = activeTool === "pan" ? "grab" : "crosshair";
        return;
    }
    if (!isPointerDown) return;

    const mode = Number(document.getElementById("selMaterial").value);
    const teamId = Number(document.getElementById("selActiveTeamBrush").value) || 1;
    const aux = getMaterialAuxParam();
    const filled = document.getElementById("chkShapeFilled").checked ? 1 : 0;

    if (activeTool === "brush") {
        Module._commit_edit_transaction();
        recordTelemetryPoint();
        renderTimelineTree();
    } else if (activeTool === "line" && dragStartCell && hoverCell) {
        Module._begin_edit_transaction();
        Module._paint_line(dragStartCell.x, dragStartCell.y, hoverCell.x, hoverCell.y, mode, teamId, aux);
        Module._commit_edit_transaction();
        recordTelemetryPoint();
        renderTimelineTree();
    } else if (activeTool === "rect" && dragStartCell && hoverCell) {
        Module._begin_edit_transaction();
        Module._paint_rect(dragStartCell.x, dragStartCell.y, hoverCell.x, hoverCell.y, mode, teamId, aux, filled);
        Module._commit_edit_transaction();
        recordTelemetryPoint();
        renderTimelineTree();
    } else if (activeTool === "circle" && dragStartCell && hoverCell) {
        const r = Math.round(Math.hypot(hoverCell.x - dragStartCell.x, hoverCell.y - dragStartCell.y));
        Module._begin_edit_transaction();
        Module._paint_circle(dragStartCell.x, dragStartCell.y, r, mode, teamId, aux, filled);
        Module._commit_edit_transaction();
        recordTelemetryPoint();
        renderTimelineTree();
    } else if (activeTool === "capture" && dragStartCell && hoverCell) {
        const w = Module._get_width(), h = Module._get_height();
        const minX = Math.max(0, Math.min(dragStartCell.x, hoverCell.x));
        const maxX = Math.min(w - 1, Math.max(dragStartCell.x, hoverCell.x));
        const minY = Math.max(0, Math.min(dragStartCell.y, hoverCell.y));
        const maxY = Math.min(h - 1, Math.max(dragStartCell.y, hoverCell.y));
        const cells = [];
        for (let y = minY; y <= maxY; y++) {
            for (let x = minX; x <= maxX; x++) {
                cells.push({
                    dx: x - minX,
                    dy: y - minY,
                    terrain: Module._get_cell_terrain(x, y),
                    teamId: Module._get_cell_team(x, y),
                    troopsFP: Module._get_cell_troops_fp(x, y),
                    growth: Module._get_cell_growth_factor(x, y),
                    flowDir: Module._get_cell_flow_dir(x, y)
                });
            }
        }
        capturedRegion = { width: maxX - minX + 1, height: maxY - minY + 1, cells };
        document.getElementById("lblCaptureStatus").textContent =
            `Region capturada (${capturedRegion.width}x${capturedRegion.height}). Asigne nombre y pulse Guardar.`;
    }

    isPointerDown = false;
    dragStartCell = null;
    lastBrushCell = null;
    renderAll();
});

canvas.addEventListener("wheel", (evt) => {
    evt.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const mx = evt.clientX - rect.left;
    const my = evt.clientY - rect.top;
    const worldX = (mx - camPanX) / camZoom;
    const worldY = (my - camPanY) / camZoom;
    const factor = evt.deltaY < 0 ? 1.16 : 0.86;
    camZoom = Math.max(0.8, Math.min(80, camZoom * factor));
    camPanX = mx - worldX * camZoom;
    camPanY = my - worldY * camZoom;
    lblZoom.textContent = `${Math.round(camZoom * 10)}%`;
    renderAll();
}, { passive: false });

minimapCanvas.addEventListener("mousedown", (evt) => {
    const rect = minimapCanvas.getBoundingClientRect();
    const w = Module._get_width(), h = Module._get_height();
    const wx = ((evt.clientX - rect.left) / minimapCanvas.width) * w;
    const wy = ((evt.clientY - rect.top) / minimapCanvas.height) * h;
    camPanX = canvas.width / 2 - wx * camZoom;
    camPanY = canvas.height / 2 - wy * camZoom;
    renderAll();
});

window.addEventListener("keydown", (evt) => {
    if (evt.target.tagName === "INPUT" || evt.target.tagName === "SELECT") return;

    if ((evt.ctrlKey || evt.metaKey) && evt.key.toLowerCase() === "z") {
        evt.preventDefault();
        stopSimulation();
        if (Module._undo_tick() === 1) {
            recordTelemetryPoint();
            renderTimelineTree();
            renderAll();
        }
    } else if ((evt.ctrlKey || evt.metaKey) && evt.key.toLowerCase() === "y") {
        evt.preventDefault();
        stopSimulation();
        if (Module._redo_tick() === 1) {
            recordTelemetryPoint();
            renderTimelineTree();
            renderAll();
        }
    } else if (evt.key.toLowerCase() === "r") {
        blueprintRotation = (blueprintRotation + 1) % 4;
        document.getElementById("lblRotation").textContent = `${blueprintRotation * 90}°`;
        renderAll();
    } else if (evt.code === "Space") {
        evt.preventDefault();
        spacePressed = true;
        canvas.style.cursor = "grab";
    }
});

window.addEventListener("keyup", (evt) => {
    if (evt.code === "Space") {
        spacePressed = false;
        canvas.style.cursor = activeTool === "pan" ? "grab" : "crosshair";
    }
});

// =========================================================================
// 7. Enlace DOM e Inicializacion
// =========================================================================

function setupUIListeners() {
    document.querySelectorAll(".tab-btn").forEach(btn => {
        btn.addEventListener("click", () => {
            document.querySelectorAll(".tab-btn").forEach(b => b.classList.remove("active"));
            document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));
            btn.classList.add("active");
            document.getElementById(btn.dataset.tab).classList.add("active");
        });
    });

    document.querySelectorAll(".tool-btn").forEach(btn => {
        btn.addEventListener("click", () => setActiveTool(btn.dataset.tool));
    });

    ["cfgDir0", "cfgDir1", "cfgDir2", "cfgDir3"].forEach(id => {
        const el = document.getElementById(id);
        DIR_LABELS.forEach((lbl, idx) => {
            const opt = document.createElement("option");
            opt.value = idx; opt.textContent = lbl;
            el.appendChild(opt);
        });
    });

    document.getElementById("selMaterial").addEventListener("change", (e) => {
        const v = Number(e.target.value);
        document.getElementById("flowDirBox").style.display = v === 9 ? "block" : "none";
        document.getElementById("growthBrushBox").style.display = v === 10 ? "block" : "none";
    });

    document.getElementById("rngGrowthBrush").addEventListener("input", (e) => {
        const v = Number(e.target.value);
        document.getElementById("lblGrowthBrush").textContent = `${(v / 64).toFixed(2)}x (${v})`;
    });

    btnPlay.addEventListener("click", () => (isRunning ? stopSimulation() : startSimulation()));

    document.getElementById("btnStep").addEventListener("click", () => {
        stopSimulation();
        Module._step_batch(Number(document.getElementById("rngBatch").value), 1);
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    });

    document.getElementById("btnUndo").addEventListener("click", () => {
        stopSimulation();
        if (Module._undo_tick() === 1) {
            recordTelemetryPoint();
            renderTimelineTree();
            renderAll();
        }
    });

    document.getElementById("btnRedo").addEventListener("click", () => {
        stopSimulation();
        if (Module._redo_tick() === 1) {
            recordTelemetryPoint();
            renderTimelineTree();
            renderAll();
        }
    });

    document.getElementById("btnResetView").addEventListener("click", () => {
        resetCamera();
        renderAll();
    });

    document.getElementById("rngFps").addEventListener("input", e => {
        document.getElementById("lblFps").textContent = e.target.value;
    });
    document.getElementById("rngBatch").addEventListener("input", e => {
        document.getElementById("lblBatch").textContent = e.target.value;
    });

    document.getElementById("btnCreateBlank").addEventListener("click", () => {
        stopSimulation();
        const w = Math.max(5, Math.min(500, Number(document.getElementById("inpNewW").value) || 200));
        const h = Math.max(5, Math.min(500, Number(document.getElementById("inpNewH").value) || 200));
        Module._init_blank_map(w, h);
        telemetryHistory = [];
        refreshTeamSelectors();
        resetCamera();
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    });

    document.getElementById("btnLoadScenario").addEventListener("click", () => {
        stopSimulation();
        const val = document.getElementById("selScenario").value;
        if (val === "__sandbox") Module._init_sandbox(80, 60);
        else if (val === "__sandbox_large") Module._init_sandbox(250, 250);
        else if (val === "__sandbox_xl") Module._init_sandbox(400, 400);
        else if (val === "__xor_10") Module._init_xor(1, 0);
        else if (val === "__xor_01") Module._init_xor(0, 1);
        else if (val === "__xor_11") Module._init_xor(1, 1);
        else if (val === "__xor_00") Module._init_xor(0, 0);
        else if (val.startsWith("custom:")) {
            const saved = getSavedScenariosMap();
            if (saved[val.slice(7)]) { loadSerializedWorld(saved[val.slice(7)]); return; }
        }
        telemetryHistory = [];
        refreshTeamSelectors();
        resetCamera();
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    });

    document.getElementById("btnSaveLocal").addEventListener("click", () => {
        const name = document.getElementById("inpSaveName").value.trim();
        if (!name) return;
        const saved = getSavedScenariosMap();
        saved[name] = serializeCurrentWorld();
        localStorage.setItem(STORAGE_SCENARIOS_KEY, JSON.stringify(saved));
        document.getElementById("inpSaveName").value = "";
        refreshScenarioDropdown();
        document.getElementById("selScenario").value = "custom:" + name;
    });

    document.getElementById("btnDeleteScenario").addEventListener("click", () => {
        const val = document.getElementById("selScenario").value;
        if (!val.startsWith("custom:")) return;
        const saved = getSavedScenariosMap();
        delete saved[val.slice(7)];
        localStorage.setItem(STORAGE_SCENARIOS_KEY, JSON.stringify(saved));
        refreshScenarioDropdown();
    });

    document.getElementById("btnExportFile").addEventListener("click", () => {
        const blob = new Blob([JSON.stringify(serializeCurrentWorld(), null, 2)], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = `escenario_tick_${Module._get_tick()}.caworld.json`;
        a.click();
        URL.revokeObjectURL(url);
    });

    document.getElementById("btnImportFile").addEventListener("click", () => {
        document.getElementById("fileImportInput").click();
    });

    document.getElementById("fileImportInput").addEventListener("change", (evt) => {
        const file = evt.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = (e) => {
            try {
                const parsed = JSON.parse(e.target.result);
                if (parsed && parsed.width && parsed.height && Array.isArray(parsed.rle)) {
                    loadSerializedWorld(parsed);
                }
            } catch (_) {
                alert("Formato de archivo .caworld.json no valido.");
            }
        };
        reader.readAsText(file);
        evt.target.value = "";
    });

    document.getElementById("selConfigTeam").addEventListener("change", loadTeamConfigToUI);

    document.getElementById("btnAddTeam").addEventListener("click", () => {
        const newId = Module._create_team();
        refreshTeamSelectors();
        document.getElementById("selConfigTeam").value = newId;
        document.getElementById("selActiveTeamBrush").value = newId;
        loadTeamConfigToUI();
        renderTelemetry();
    });

    document.getElementById("btnDeleteTeam").addEventListener("click", () => {
        const t = Number(document.getElementById("selConfigTeam").value);
        if (!t) return;
        Module._delete_team(t);
        refreshTeamSelectors();
        recordTelemetryPoint();
        renderTimelineTree();
        renderAll();
    });

    document.getElementById("btnApplyTeamConfig").addEventListener("click", () => {
        const t = Number(document.getElementById("selConfigTeam").value);
        if (!t) return;
        const rgb = hexToRgb(document.getElementById("inpTeamColor").value);
        Module._set_team_color(t, rgb.r, rgb.g, rgb.b);
        Module._set_team_config(
            t,
            Number(document.getElementById("cfgDir0").value),
            Number(document.getElementById("cfgDir1").value),
            Number(document.getElementById("cfgDir2").value),
            Number(document.getElementById("cfgDir3").value),
            Number(document.getElementById("cfgTau").value),
            Number(document.getElementById("cfgPeriod").value),
            Number(document.getElementById("cfgMaxArea").value),
            Number(document.getElementById("cfgFriction").value),
            document.getElementById("cfgMultiDir").checked ? 1 : 0,
            Number(document.getElementById("cfgRegen").value),
            Number(document.getElementById("cfgMaxTroops").value),
            document.getElementById("cfgOrganic").checked ? 1 : 0
        );
        renderAll();
        renderTelemetry();
    });

    document.getElementById("btnRotateBp").addEventListener("click", () => {
        blueprintRotation = (blueprintRotation + 1) % 4;
        document.getElementById("lblRotation").textContent = `${blueprintRotation * 90}°`;
        renderAll();
    });

    document.getElementById("btnSaveCapturedBp").addEventListener("click", () => {
        const name = document.getElementById("inpBpName").value.trim();
        if (!capturedRegion || !name) return;
        blueprints.push({
            name,
            width: capturedRegion.width,
            height: capturedRegion.height,
            cells: capturedRegion.cells,
            builtin: false
        });
        saveCustomBlueprints();
        selectedBlueprintIdx = blueprints.length - 1;
        document.getElementById("inpBpName").value = "";
        document.getElementById("lblCaptureStatus").textContent = `Plantilla "${name}" guardada.`;
        renderBlueprintList();
        setActiveTool("stamp");
    });

    document.getElementById("chkGraphOverlay").addEventListener("change", renderAll);
    document.getElementById("chkShowGrid").addEventListener("change", renderAll);
    window.addEventListener("resize", resizeCanvas);
}

Module.onRuntimeInitialized = () => {
    setupUIListeners();
    initBlueprints();
    refreshScenarioDropdown();
    Module._init_sandbox(80, 60);
    refreshTeamSelectors();
    resizeCanvas();
    resetCamera();
    recordTelemetryPoint();
    renderTimelineTree();
    renderAll();
};