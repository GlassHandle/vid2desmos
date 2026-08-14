'use strict';

const state = {
  meta: null,
  data: null,
  starts: null,
  frame: 0,
  playing: false,
  grid: true,
  whiteOnBlack: true,

  cx: 0, cy: 0, scale: 1, userAdjusted: false,
};

const canvas = document.getElementById('graph');
const ctx = canvas.getContext('2d', { alpha: false });
let dpr = 1;

async function load() {
  const meta = await (await fetch('api/meta')).json();
  const [d, s] = await Promise.all([
    fetch('api/d.bin').then(r => r.arrayBuffer()),
    fetch('api/s.bin').then(r => r.arrayBuffer()),
  ]);
  state.meta = meta;
  state.data = new Float64Array(d);
  state.starts = new Float64Array(s);
}

function regionsOf(frame) {
  const { gridW, gridH } = state.meta;
  const lo = state.starts[frame] - 1;
  const hi = state.starts[frame + 1] - 1;
  const n = hi - lo;
  const out = new Int32Array(n * 4);

  const hR = gridH + 1;
  const wR = gridW + 1;
  for (let i = 0; i < n; i++) {
    const p = state.data[lo + i];
    const h = p % hR;
    const t = Math.floor(p / hR);
    const w = t % wR;
    const q = Math.floor(t / wR);
    out[i * 4]     = q % gridW;
    out[i * 4 + 1] = Math.floor(q / gridW);
    out[i * 4 + 2] = w;
    out[i * 4 + 3] = h;
  }
  return out;
}

function regionCount(frame) {
  return state.starts[frame + 1] - state.starts[frame];
}

function resize() {
  dpr = window.devicePixelRatio || 1;
  const r = canvas.getBoundingClientRect();
  if (r.width < 1 || r.height < 1) return;
  canvas.width = Math.round(r.width * dpr);
  canvas.height = Math.round(r.height * dpr);
  if (state.userAdjusted) draw(); else home();
}

function home() {
  const { gridW, gridH } = state.meta;
  const w = canvas.width / dpr;
  const h = canvas.height / dpr;
  if (w < 1 || h < 1) return;
  state.scale = Math.min(w / (gridW * 1.14), h / (gridH * 1.14));
  state.cx = gridW / 2;
  state.cy = gridH / 2;
  state.userAdjusted = false;
  draw();
}

const toScreenX = x => (x - state.cx) * state.scale + canvas.width / dpr / 2;
const toScreenY = y => canvas.height / dpr / 2 - (y - state.cy) * state.scale;
const toWorldX = px => (px - canvas.width / dpr / 2) / state.scale + state.cx;
const toWorldY = py => (canvas.height / dpr / 2 - py) / state.scale + state.cy;

function zoomAt(px, py, factor) {
  state.userAdjusted = true;
  const wx = toWorldX(px), wy = toWorldY(py);
  state.scale = Math.max(0.05, Math.min(400, state.scale * factor));

  state.cx = wx - (px - canvas.width / dpr / 2) / state.scale;
  state.cy = wy + (py - canvas.height / dpr / 2) / state.scale;
  draw();
}

function niceStep(minPx) {
  const target = minPx / state.scale;
  const pow = Math.pow(10, Math.floor(Math.log10(target)));
  for (const m of [1, 2, 5, 10]) {
    if (pow * m >= target) return pow * m;
  }
  return pow * 10;
}

function drawGrid(w, h) {
  const step = niceStep(58);
  const minor = step / 5;
  const x0 = toWorldX(0), x1 = toWorldX(w);
  const y0 = toWorldY(h), y1 = toWorldY(0);

  ctx.lineWidth = 1;

  if (minor * state.scale > 6) {
    ctx.strokeStyle = '#f0f0f0';
    ctx.beginPath();
    for (let x = Math.ceil(x0 / minor) * minor; x <= x1; x += minor) {
      const sx = Math.round(toScreenX(x)) + 0.5;
      ctx.moveTo(sx, 0); ctx.lineTo(sx, h);
    }
    for (let y = Math.ceil(y0 / minor) * minor; y <= y1; y += minor) {
      const sy = Math.round(toScreenY(y)) + 0.5;
      ctx.moveTo(0, sy); ctx.lineTo(w, sy);
    }
    ctx.stroke();
  }

  ctx.strokeStyle = '#dcdcdc';
  ctx.beginPath();
  for (let x = Math.ceil(x0 / step) * step; x <= x1; x += step) {
    const sx = Math.round(toScreenX(x)) + 0.5;
    ctx.moveTo(sx, 0); ctx.lineTo(sx, h);
  }
  for (let y = Math.ceil(y0 / step) * step; y <= y1; y += step) {
    const sy = Math.round(toScreenY(y)) + 0.5;
    ctx.moveTo(0, sy); ctx.lineTo(w, sy);
  }
  ctx.stroke();

  ctx.strokeStyle = '#8a8a8a';
  ctx.beginPath();
  const ax = Math.round(toScreenX(0)) + 0.5;
  const ay = Math.round(toScreenY(0)) + 0.5;
  ctx.moveTo(ax, 0); ctx.lineTo(ax, h);
  ctx.moveTo(0, ay); ctx.lineTo(w, ay);
  ctx.stroke();

  ctx.fillStyle = '#767676';
  ctx.font = '11px -apple-system, "Segoe UI", Roboto, sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (let x = Math.ceil(x0 / step) * step; x <= x1; x += step) {
    if (Math.abs(x) < 1e-9) continue;
    ctx.fillText(String(+x.toFixed(6)), toScreenX(x), Math.min(h - 14, Math.max(2, ay + 3)));
  }
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let y = Math.ceil(y0 / step) * step; y <= y1; y += step) {
    if (Math.abs(y) < 1e-9) continue;
    ctx.fillText(String(+y.toFixed(6)), Math.max(24, Math.min(w - 3, ax - 5)), toScreenY(y));
  }
}

function draw() {
  if (!state.meta) return;
  const w = canvas.width / dpr;
  const h = canvas.height / dpr;

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, w, h);

  if (state.grid) drawGrid(w, h);

  const { gridW, gridH } = state.meta;
  const bg = state.whiteOnBlack ? '#000000' : '#ffffff';
  const ink = state.whiteOnBlack ? '#ffffff' : '#000000';

  const bx = toScreenX(0), by = toScreenY(gridH);
  const bw = gridW * state.scale, bh = gridH * state.scale;
  ctx.fillStyle = bg;
  ctx.fillRect(bx, by, bw, bh);
  if (!state.whiteOnBlack) {
    ctx.strokeStyle = '#cfcfcf';
    ctx.lineWidth = 1;
    ctx.strokeRect(bx + 0.5, by + 0.5, bw, bh);
  }

  const r = regionsOf(state.frame);
  ctx.fillStyle = ink;
  const s = state.scale;
  for (let i = 0; i < r.length; i += 4) {
    const rw = r[i + 2], rh = r[i + 3];
    if (rw === 0 || rh === 0) continue;
    const sx = toScreenX(r[i]);
    const sy = toScreenY(r[i + 1] + rh);

    const px = Math.floor(sx), py = Math.floor(sy);
    ctx.fillRect(px, py, Math.ceil(sx + rw * s) - px, Math.ceil(sy + rh * s) - py);
  }

  updateReadout();
}

function updateReadout() {
  const n = regionCount(state.frame);
  document.getElementById('readout').textContent =
    `frame ${state.frame} / ${state.meta.frames - 1}   ·   ${n} region${n === 1 ? '' : 's'}` +
    `   ·   ${(state.scale).toFixed(1)} px/unit`;
}

function mathHtml(text) {
  const esc = text.replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));

  return esc.replace(/_\{([^}]*)\}/g, '<sub>$1</sub>');
}

function buildPanel() {
  const list = document.getElementById('exprs');
  list.innerHTML = '';

  for (const row of state.meta.expressions) {
    const li = document.createElement('li');

    const gut = document.createElement('div');
    gut.className = 'gut';
    if (row.kind === 'backdrop' || row.kind === 'regions') {
      gut.classList.add('swatch');
      const dot = document.createElement('i');
      dot.dataset.role = row.kind;
      gut.appendChild(dot);
    }
    li.appendChild(gut);

    const body = document.createElement('div');
    body.className = 'body';

    const math = document.createElement('div');
    math.className = 'math';
    math.innerHTML = mathHtml(row.text);
    body.appendChild(math);

    if (row.note) {
      const note = document.createElement('div');
      note.className = 'note';
      note.textContent = row.note;
      body.appendChild(note);
    }

    if (row.kind === 'slider') {
      const wrap = document.createElement('div');
      wrap.className = 'sliderrow';

      const play = document.createElement('button');
      play.id = 'play';
      play.textContent = '▶';
      play.addEventListener('click', togglePlay);

      const range = document.createElement('input');
      range.type = 'range';
      range.id = 'fslider';
      range.min = '0';
      range.max = String(state.meta.frames - 1);
      range.step = '1';
      range.value = '0';
      range.addEventListener('input', () => {
        setFrame(Number(range.value));
        stop();
      });

      wrap.append(play, range);
      body.appendChild(wrap);
    }

    li.appendChild(body);
    list.appendChild(li);
  }

  paintSwatches();

  const m = state.meta;
  document.getElementById('stats').innerHTML =
    `<b>${m.frames}</b> frames · <b>${m.gridW}×${m.gridH}</b> grid · <b>${m.fps}</b> fps<br>` +
    `<b>${m.regions.toLocaleString()}</b> regions total · ` +
    `${Math.round(m.avgRegions)} avg · ${m.maxRegions} max<br>` +
    `${m.parts.length} part${m.parts.length === 1 ? '' : 's'} · ` +
    `expressions shown for ${m.parts[0].name}`;
}

function paintSwatches() {
  document.querySelectorAll('#exprs .gut.swatch i').forEach(dot => {
    const isBackdrop = dot.dataset.role === 'backdrop';
    const black = state.whiteOnBlack ? isBackdrop : !isBackdrop;
    dot.style.background = black ? '#000' : '#fff';
  });
}

function setFrame(i) {
  const n = state.meta.frames;
  state.frame = ((i % n) + n) % n;
  const slider = document.getElementById('fslider');
  if (slider) slider.value = String(state.frame);
  draw();
}

let raf = null;
let lastTick = 0;

function tick(now) {
  if (!state.playing) return;
  const step = 1000 / Math.max(state.meta.fps, 0.1);

  let steps = Math.floor((now - lastTick) / step + 1e-9);
  if (steps > 0) {

    if (steps > 4) {
      steps = 1;
      lastTick = now;
    } else {
      lastTick += steps * step;
    }
    setFrame(state.frame + steps);
  }
  raf = requestAnimationFrame(tick);
}

function play() {
  if (state.playing) return;
  state.playing = true;
  lastTick = performance.now();
  const b = document.getElementById('play');
  if (b) b.textContent = '❚❚';
  raf = requestAnimationFrame(tick);
}

function stop() {
  if (!state.playing) return;
  state.playing = false;
  if (raf) cancelAnimationFrame(raf);
  const b = document.getElementById('play');
  if (b) b.textContent = '▶';
}

function togglePlay() { state.playing ? stop() : play(); }

function wireControls() {
  document.getElementById('zoom-in').onclick =
    () => zoomAt(canvas.width / dpr / 2, canvas.height / dpr / 2, 1.25);
  document.getElementById('zoom-out').onclick =
    () => zoomAt(canvas.width / dpr / 2, canvas.height / dpr / 2, 0.8);
  document.getElementById('home').onclick = home;

  const gridBtn = document.getElementById('grid');
  gridBtn.onclick = () => {
    state.grid = !state.grid;
    gridBtn.classList.toggle('on', state.grid);
    draw();
  };

  document.getElementById('ink').onclick = () => {
    state.whiteOnBlack = !state.whiteOnBlack;
    paintSwatches();
    draw();
  };

  document.getElementById('shot').onclick = () => {
    canvas.toBlob(blob => {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `frame_${String(state.frame).padStart(4, '0')}.png`;
      a.click();
      URL.revokeObjectURL(a.href);
    });
  };

  const app = document.getElementById('app');
  const expand = document.getElementById('expand');
  document.getElementById('collapse').onclick = () => {
    app.classList.add('collapsed');
    expand.hidden = false;
    setTimeout(resize, 200);
  };
  expand.onclick = () => {
    app.classList.remove('collapsed');
    expand.hidden = true;
    setTimeout(resize, 200);
  };

  let dragging = false, lastX = 0, lastY = 0;
  canvas.addEventListener('pointerdown', e => {
    dragging = true;
    lastX = e.offsetX; lastY = e.offsetY;
    canvas.classList.add('dragging');
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener('pointermove', e => {
    if (!dragging) return;
    state.userAdjusted = true;
    state.cx -= (e.offsetX - lastX) / state.scale;
    state.cy += (e.offsetY - lastY) / state.scale;
    lastX = e.offsetX; lastY = e.offsetY;
    draw();
  });
  const endDrag = () => { dragging = false; canvas.classList.remove('dragging'); };
  canvas.addEventListener('pointerup', endDrag);
  canvas.addEventListener('pointercancel', endDrag);

  canvas.addEventListener('wheel', e => {
    e.preventDefault();
    zoomAt(e.offsetX, e.offsetY, e.deltaY < 0 ? 1.1 : 1 / 1.1);
  }, { passive: false });

  window.addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT' && e.key !== ' ') return;
    if (e.key === ' ') { e.preventDefault(); togglePlay(); }
    else if (e.key === 'ArrowRight') { stop(); setFrame(state.frame + 1); }
    else if (e.key === 'ArrowLeft') { stop(); setFrame(state.frame - 1); }
    else if (e.key === 'Home') { stop(); setFrame(0); }
    else if (e.key === 'End') { stop(); setFrame(state.meta.frames - 1); }
  });

  window.addEventListener('resize', resize);
  if (window.ResizeObserver) {
    new ResizeObserver(resize).observe(document.getElementById('stage'));
  }
}

(async function main() {
  const splash = document.getElementById('loading');
  try {
    await load();
  } catch (err) {
    splash.className = 'error';
    splash.textContent = 'Could not load frame data: ' + err;
    return;
  }
  buildPanel();
  wireControls();
  resize();
  home();
  splash.className = 'done';
})();
