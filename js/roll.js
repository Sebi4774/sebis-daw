// Sebi's DAW mobile - the Edit screen: piano roll for instruments, pad grid for drums, settings for audio clips.
import { S, on, emit, send, sheet, toast, h, pushUndo, markDirty, syncTrack, cssColour, selTrack, selClip, noteName, nowBeat, norm } from './ctx.js';
import { fitCanvas, gestures } from './canvas.js';
import { roundRect } from './arrange.js';

const RH = 24;
let cv, view = { x: 0, y: -1, ppb: 64 }, lastKey = null;
const GRIDS = [['1/4', 1], ['1/8', 0.5], ['1/16', 0.25], ['1/32', 0.125], ['1/8T', 1 / 3], ['1/16T', 1 / 6]];
const drums = () => selTrack() && selTrack().type === 'drums';
const KW = () => (drums() ? 86 : 44);
const ROWH = () => (drums() ? 34 : 20);

export function initRoll() {
  cv = document.getElementById('rollCanvas');
  gestures(cv, { down, tap, longPress, drag, up, pinch });
  on('tick', () => { if (S.view === 'edit') draw(); });
  on('select', () => { if (lastKey !== key()) view.y = -1; });
  window.addEventListener('resize', () => draw());
}
const key = () => `${S.sel}:${S.clip}`;
// rows: drums = pads 0..15 top to bottom (note 36 + i); instruments = pitch 127 at the top
const rowOf = pitch => (drums() ? pitch - 36 : 127 - pitch);
const pitchOf = row => (drums() ? 36 + row : 127 - row);
const rows = () => (drums() ? 16 : 128);
const beatAt = x => view.x + (x - KW()) / view.ppb;
const xOf = b => KW() + (b - view.x) * view.ppb;
const rowAt = y => Math.floor((y - RH + view.y) / ROWH());
const snapDown = b => Math.floor(b / S.grid + 1e-6) * S.grid;

function noteAt(p) {
  const c = selClip(); if (!c || c.kind !== 'midi') return null;
  const b = beatAt(p.x), pitch = pitchOf(rowAt(p.y));
  for (let i = c.notes.length - 1; i >= 0; i--) {
    const n = c.notes[i];
    if (n.pitch === pitch && b >= n.start && b < n.start + Math.max(n.length, 8 / view.ppb)) return { i, n, edge: xOf(n.start + n.length) - p.x < Math.min(16, n.length * view.ppb * 0.4) };
  }
  return null;
}
function preview(pitch, vel = S.vel) {
  send('live', { track: S.sel, note: pitch, vel, on: true });
  setTimeout(() => send('live', { track: S.sel, note: pitch, vel: 0, on: false }), 220);
}
function down(p) {
  const c = selClip(); if (!c || c.kind !== 'midi') return { kind: 'none' };
  if (p.y < RH) return { kind: 'ruler', b: beatAt(p.x) };
  if (p.x < KW()) return { kind: 'keys', row: rowAt(p.y), y0: view.y };
  const hit = noteAt(p);
  if (hit) return { kind: 'note', ...hit, s0: hit.n.start, l0: hit.n.length, p0: hit.n.pitch, undone: false };
  return { kind: 'empty', x0: view.x, y0: view.y, b: beatAt(p.x), row: rowAt(p.y) };
}
function tap(p, st) {
  const c = selClip();
  if (st.kind === 'ruler') { const b = Math.max(0, c.start + Math.floor(st.b)); send('seek', { beat: b }); S.pos = b; return; }
  if (st.kind === 'keys') { if (st.row >= 0 && st.row < rows()) preview(pitchOf(st.row)); return; }
  if (st.kind === 'note') { pushUndo(); c.notes.splice(st.i, 1); commit(); return; }
  if (st.kind === 'empty') {
    if (st.row < 0 || st.row >= rows() || st.b < 0) return;
    if (st.b >= c.length) { toast('Past the end of the clip: make it longer with + Bar'); return; }
    pushUndo();
    const pitch = pitchOf(st.row);
    c.notes.push({ start: snapDown(st.b), length: drums() ? Math.min(S.noteLen, 0.25) : S.noteLen, pitch, velocity: S.vel });
    preview(pitch); commit();
  }
}
function longPress(p, st) {
  if (st.kind !== 'note') return;
  const c = selClip(), n = st.n;
  const setV = v => { pushUndo(); n.velocity = v; commit(); };
  sheet(`${drums() ? padName(n.pitch) : noteName(n.pitch)} · velocity ${Math.round(n.velocity * 100)}%`, [
    { label: 'Soft (45%)', fn: () => setV(0.45) }, { label: 'Medium (75%)', fn: () => setV(0.75) }, { label: 'Hard (100%)', fn: () => setV(1) },
    { label: 'Delete note', danger: true, fn: () => { pushUndo(); c.notes.splice(c.notes.indexOf(n), 1); commit(); } },
  ]);
}
function drag(p, st, dx, dy) {
  const c = selClip();
  if (st.kind === 'empty' || st.kind === 'keys') {
    if (st.kind === 'empty') view.x = Math.max(-0.5, st.x0 - dx / view.ppb);
    view.y = clampY(st.y0 - dy); draw(); return;
  }
  if (st.kind === 'note') {
    if (!st.undone) { pushUndo(); st.undone = true; }
    if (st.edge) st.n.length = Math.max(S.grid / 2, Math.round((st.l0 + dx / view.ppb) / S.grid) * S.grid || S.grid);
    else {
      st.n.start = Math.max(0, Math.min(c.length - S.grid / 4, st.s0 + Math.round(dx / view.ppb / S.grid) * S.grid));
      const np = pitchOf(Math.max(0, Math.min(rows() - 1, rowAt(p.y))));
      if (np !== st.n.pitch) { st.n.pitch = np; preview(np, st.n.velocity); }
    }
    draw();
  }
}
function up(p, st, moved) { if (st.kind === 'note' && moved) commit(); }
function pinch(scale, cx) { const b = beatAt(cx); view.ppb = Math.max(12, Math.min(400, view.ppb * scale)); view.x = Math.max(-0.5, b - (cx - KW()) / view.ppb); draw(); }
function clampY(y) { return Math.max(0, Math.min(rows() * ROWH() - (cv.clientHeight - RH) + 10, y)); }
function commit() {
  const c = selClip(); c.notes.sort((a, b) => a.start - b.start || a.pitch - b.pitch);
  syncTrack(S.sel, 'clips'); markDirty(); draw();
}
function padName(pitch) { const t = selTrack(); const p = t.pads.find(q => q.i === pitch - 36); return p ? p.name : 'Pad ' + (pitch - 35); }

export function rollToolbar(tb) {
  const c = selClip();
  if (!c) { tb.append(h('span', { class: 'label' }, 'Pick a clip in Arrange (tap it twice) to edit its notes')); return; }
  if (c.kind === 'audio') { tb.append(h('button', { onclick: () => emit('view', 'arrange') }, '‹ Arrange'), h('span', { class: 'label' }, 'Audio clip settings')); return; }
  const gridSel = h('select', { onchange: e => { S.grid = +e.target.value; draw(); } }, GRIDS.map(([n, v]) => h('option', { value: v, selected: Math.abs(v - S.grid) < 1e-9 }, 'Grid ' + n)));
  const lenSel = h('select', { onchange: e => { S.noteLen = +e.target.value; } },
    [...GRIDS, ['1/2', 2], ['1 bar', 4]].map(([n, v]) => h('option', { value: v, selected: Math.abs(v - S.noteLen) < 1e-9 }, 'Note ' + n)));
  const velSel = h('select', { onchange: e => { S.vel = +e.target.value; } }, [['Soft', 0.45], ['Med', 0.75], ['Hard', 1]].map(([n, v]) => h('option', { value: v, selected: Math.abs(v - S.vel) < 0.1 }, n)));
  const bars = d => { pushUndo(); c.length = Math.max(1, c.length + d); syncTrack(S.sel, 'clips'); markDirty(); emit('toolbar'); draw(); };
  tb.append(
    h('button', { onclick: () => emit('view', 'arrange') }, '‹'),
    gridSel, lenSel, velSel,
    h('span', { class: 'sep' }),
    h('button', { onclick: () => bars(-4) }, '− Bar'), h('span', { class: 'label' }, `${+(c.length / 4).toFixed(2)} bars`), h('button', { onclick: () => bars(4) }, '+ Bar'),
    h('span', { class: 'sep' }),
    h('button', { onclick: () => { pushUndo(); for (const n of c.notes) n.start = Math.max(0, Math.round(n.start / S.grid) * S.grid); commit(); toast('Quantized to the grid'); } }, 'Quantize'),
    h('button', { onclick: () => { if (!c.notes.length) return; pushUndo(); c.notes.length = 0; commit(); } }, 'Clear'),
  );
}

// audio clip: the same settings as Studio's audio clip panel
export function renderAudioEdit() {
  const box = document.getElementById('audioEdit');
  const c = selClip();
  const show = !!c && c.kind === 'audio';
  box.classList.toggle('hidden', !show); cv.classList.toggle('hidden', show);
  if (!show) return;
  box.innerHTML = '';
  const t = selTrack();
  const set = (k, v) => { pushUndo(); c[k] = v; syncTrack(S.sel, 'clips'); markDirty(); };
  const num = (label, k, step, min, max) => h('div', { class: 'slider' }, h('label', {}, label),
    h('input', { type: 'number', step, min, max, value: +(+c[k]).toFixed(3), onchange: e => set(k, Math.min(max, Math.max(min, +e.target.value || 0))) }), h('output', {}, ''));
  box.append(
    h('h2', {}, c.name || 'Audio clip'),
    h('p', { class: 'note' }, (c.file || '').split(/[\\/]/).pop()),
    h('div', { class: 'row' }, h('button', { class: c.stretch ? 'on' : '', onclick: () => { set('stretch', !c.stretch); renderAudioEdit(); } }, c.stretch ? 'Follows song tempo' : 'Plays at its own speed')),
    num('File tempo', 'sourceBpm', 0.1, 20, 400),
    num('Transpose', 'transpose', 1, -24, 24),
    num('Gain', 'gain', 0.05, 0, 4),
    num('Start in file (s)', 'offset', 0.01, 0, 3600),
    num('Length (beats)', 'length', 1, 0.25, 4096),
    h('p', { class: 'note' }, `On ${t.name}. Tempo changes re-stretch the clip with the pitch kept, like on the computer.`),
  );
}

export function draw() {
  if (!cv || S.view !== 'edit') return;
  const c = selClip(), t = selTrack();
  renderAudioEdit();
  if (c && c.kind === 'audio') return;
  const { g, w, h: H } = fitCanvas(cv);
  g.fillStyle = '#0d0b14'; g.fillRect(0, 0, w, H);
  if (!c) { g.fillStyle = '#9a93b0'; g.font = '14px -apple-system,sans-serif'; g.fillText('No clip picked. In Arrange, tap a clip, then tap it again.', 16, 40); return; }
  if (view.y < 0 || lastKey !== key()) {   // first look: centre on the notes (or C3-C5)
    lastKey = key(); view.x = -0.25;
    const ps = c.notes.map(n => rowOf(n.pitch));
    const mid = ps.length ? (Math.min(...ps) + Math.max(...ps)) / 2 : rowOf(drums() ? 40 : 60);
    view.y = 0; view.y = clampY(mid * ROWH() - (H - RH) / 2);
    view.ppb = Math.max(24, Math.min(120, (w - KW()) / Math.min(8, Math.max(2, c.length))));
  }
  const kw = KW(), rh = ROWH(), col = cssColour(t.colour);
  const r0 = Math.max(0, Math.floor(view.y / rh)), r1 = Math.min(rows() - 1, Math.ceil((view.y + H) / rh));
  g.save(); g.beginPath(); g.rect(kw, RH, w - kw, H - RH); g.clip();
  for (let r = r0; r <= r1; r++) {
    const y = RH + r * rh - view.y, pc = pitchOf(r) % 12;
    g.fillStyle = drums() ? (r % 2 ? '#110f1a' : '#0d0b14') : ([1, 3, 6, 8, 10].includes(pc) ? '#0b0910' : '#13101d');
    g.fillRect(kw, y, w - kw, rh);
    if (!drums() && pc === 0) { g.fillStyle = '#2c2640'; g.fillRect(kw, y + rh - 1, w - kw, 1); }
  }
  // grid lines
  const b0 = beatAt(kw), b1 = beatAt(w);
  for (let b = Math.floor(b0 / S.grid) * S.grid; b <= b1; b += S.grid) {
    const x = xOf(b), onBeat = Math.abs(b - Math.round(b)) < 1e-6;
    g.fillStyle = onBeat && Math.round(b) % 4 === 0 ? '#3a3254' : onBeat ? '#251f38' : '#17131f';
    g.fillRect(x, RH, 1, H - RH);
  }
  // past the clip end
  const ex = xOf(c.length); if (ex < w) { g.fillStyle = 'rgba(0,0,0,.55)'; g.fillRect(ex, RH, w - ex, H - RH); g.fillStyle = col; g.fillRect(ex, RH, 2, H - RH); }
  // notes
  for (const n of c.notes) {
    const y = RH + rowOf(n.pitch) * rh - view.y; if (y < RH - rh || y > H) continue;
    const x = xOf(n.start), nw = Math.max(4, n.length * view.ppb - 2);
    g.globalAlpha = n.start >= c.length ? 0.3 : 0.45 + 0.55 * n.velocity;
    g.fillStyle = col; roundRect(g, x + 1, y + 1, nw, rh - 2, 4); g.fill();
    g.globalAlpha = 1;
    g.fillStyle = 'rgba(255,255,255,.55)'; g.fillRect(x + nw - 3, y + 4, 2, rh - 8);
  }
  // playhead inside the clip
  const pb = nowBeat() - c.start;
  if (pb >= 0 && pb <= c.length) { g.fillStyle = '#7be3a5'; g.fillRect(xOf(pb), RH, 1.5, H - RH); }
  g.restore();
  // ruler (beats of the clip)
  g.fillStyle = '#15121f'; g.fillRect(0, 0, w, RH);
  g.font = '11px ui-monospace,Menlo,monospace'; g.fillStyle = '#9a93b0';
  const step = view.ppb < 20 ? 4 : 1;
  for (let b = Math.max(0, Math.floor(b0)); b <= b1; b += step) { const x = xOf(b); if (x >= kw - 2) g.fillText(`${Math.floor(b / 4) + 1}.${(b % 4) + 1}`, x + 3, 16); }
  // keys / pad names
  g.save(); g.beginPath(); g.rect(0, RH, kw, H - RH); g.clip();
  for (let r = r0; r <= r1; r++) {
    const y = RH + r * rh - view.y, p = pitchOf(r), pc = p % 12;
    if (drums()) {
      g.fillStyle = r % 2 ? '#1a1626' : '#15121f'; g.fillRect(0, y, kw, rh);
      g.fillStyle = '#ece8f6'; g.font = '12px -apple-system,sans-serif'; g.fillText(padName(p).slice(0, 11), 8, y + rh / 2 + 4);
    } else {
      const black = [1, 3, 6, 8, 10].includes(pc);
      g.fillStyle = black ? '#1b1727' : '#d9d4e6'; g.fillRect(0, y, kw, rh - 1);
      if (pc === 0) { g.fillStyle = '#5a527a'; g.font = '10px -apple-system,sans-serif'; g.fillText(noteName(p), 4, y + rh - 5); }
    }
  }
  g.restore();
  g.fillStyle = '#2c2640'; g.fillRect(kw - 1, 0, 1, H); g.fillRect(0, RH - 1, w, 1);
}
