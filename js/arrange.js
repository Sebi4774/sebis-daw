// Sebi's DAW mobile - the Arrange screen: tracks down the side, clips on a bar grid.
import { S, on, emit, send, sheet, toast, h, pushUndo, markDirty, syncTrack, syncSong, cssColour, newTrack, selTrack, pushSong, undo, redo, nowBeat } from './ctx.js';
import { fitCanvas, gestures } from './canvas.js';

const HW = 104, RH = 28, ROW = 60;
let cv, view = { x: 0, y: 0, ppb: 18 };   // scroll in beats / px, pixels per beat
export const arrangeView = view;

export function initArrange() {
  cv = document.getElementById('arrCanvas');
  gestures(cv, { down, tap, longPress, drag, up, pinch });
  on('tick', () => { if (S.view === 'arrange') draw(); });
  on('song', () => draw());
  window.addEventListener('resize', () => draw());
}

const beatAt = x => view.x + (x - HW) / view.ppb;
const xOf = b => HW + (b - view.x) * view.ppb;
const rowAt = y => Math.floor((y - RH + view.y) / ROW);
function clipAt(p) {
  const r = rowAt(p.y), t = S.song.tracks[r];
  if (!t || p.x < HW || p.y < RH) return null;
  const b = beatAt(p.x);
  for (let i = t.clips.length - 1; i >= 0; i--) { const c = t.clips[i]; if (b >= c.start && b < c.start + c.length) return { r, i, c, edge: xOf(c.start + c.length) - p.x < 18 }; }
  return null;
}

function down(p) {
  if (p.y < RH && p.x >= HW) return { kind: 'ruler', b0: beatAt(p.x) };
  if (p.x < HW) return { kind: 'header', r: rowAt(p.y), y0: view.y };
  const hit = clipAt(p);
  if (hit) return { kind: 'clip', ...hit, start0: hit.c.start, len0: hit.c.length, track0: hit.r, undone: false };
  return { kind: 'empty', x0: view.x, y0: view.y, r: rowAt(p.y), b: beatAt(p.x) };
}
function tap(p, st) {
  if (st.kind === 'ruler') { const b = Math.max(0, Math.floor(st.b0)); send('seek', { beat: b }); S.pos = b; draw(); return; }
  if (st.kind === 'header') { if (st.r < 0 || st.r >= S.song.tracks.length) return; if (S.sel === st.r) trackMenu(st.r); else select(st.r, -1); return; }
  if (st.kind === 'clip') { if (S.sel === st.r && S.clip === st.i) openEditor(); else select(st.r, st.i); return; }
  if (st.kind === 'empty') {
    const t = S.song.tracks[st.r];
    if (!t) return;
    if (S.clip >= 0) { select(st.r, -1); return; }
    if (t.type === 'audio') { select(st.r, -1); emit('importAudio', { track: st.r, beat: Math.floor(st.b / 4) * 4 }); return; }
    if (t.type === 'plugin') { toast('This VST3 track plays on the computer only. Its notes still edit here.'); }
    pushUndo();
    const start = Math.max(0, Math.floor(st.b / 4) * 4);
    t.clips.push({ attrs: {}, kind: 'midi', name: t.name, start, length: 4, gain: 1, notes: [] });
    select(st.r, t.clips.length - 1);
    syncTrack(st.r, 'clips'); markDirty();
  }
}
function longPress(p, st) {
  if (st.kind === 'clip') { select(st.r, st.i); clipMenu(); }
  else if (st.kind === 'header' && S.song.tracks[st.r]) { select(st.r, -1); trackMenu(st.r); }
  else if (st.kind === 'ruler') loopMenu();
}
function drag(p, st, dx, dy) {
  if (st.kind === 'empty' || st.kind === 'header') {
    if (st.kind === 'empty') view.x = Math.max(0, st.x0 - dx / view.ppb);
    view.y = Math.max(0, Math.min(Math.max(0, S.song.tracks.length * ROW - (cv.clientHeight - RH) + 20), st.y0 - dy));
    draw(); return;
  }
  if (st.kind === 'ruler') {   // drag on the ruler = set the loop
    const a = Math.max(0, Math.floor(Math.min(st.b0, beatAt(p.x)) / 4) * 4), b = Math.ceil(Math.max(st.b0, beatAt(p.x)) / 4) * 4;
    S.song.loopStart = a; S.song.loopEnd = Math.max(a + 4, b); S.song.loopOn = true; syncSong(); emit('toolbar'); draw(); return;
  }
  if (st.kind === 'clip') {
    if (!st.undone) { pushUndo(); st.undone = true; }
    const t0 = S.song.tracks[st.track0];
    if (st.edge) st.c.length = Math.max(1, Math.round((st.len0 + dx / view.ppb)));
    else {
      st.c.start = Math.max(0, Math.round(st.start0 + dx / view.ppb));
      // move to another track of the same kind
      const r = Math.max(0, Math.min(S.song.tracks.length - 1, rowAt(p.y)));
      const cur = S.song.tracks.findIndex(t => t.clips.includes(st.c));
      const target = S.song.tracks[r];
      if (r !== cur && (target.type === 'audio') === (st.c.kind === 'audio')) {
        const from = S.song.tracks[cur]; from.clips.splice(from.clips.indexOf(st.c), 1); target.clips.push(st.c);
        syncTrack(cur, 'clips'); S.sel = r; S.clip = target.clips.length - 1;
      }
    }
    draw();
  }
}
function up(p, st, moved) {
  if (st.kind === 'clip' && moved) {
    const r = S.song.tracks.findIndex(t => t.clips.includes(st.c));
    S.sel = r; S.clip = S.song.tracks[r].clips.indexOf(st.c);
    syncTrack(r, 'clips'); markDirty(); emit('select');
  }
  if (st.kind === 'ruler' && moved) markDirty();
}
function pinch(scale, cx) {
  const b = beatAt(cx);
  view.ppb = Math.max(3, Math.min(120, view.ppb * scale));
  view.x = Math.max(0, b - (cx - HW) / view.ppb);
  draw();
}

export function select(r, c) { S.sel = r; S.clip = c; emit('select'); draw(); }
function openEditor() { emit('view', 'edit'); }

export function addTrackMenu() {
  const add = (type, name, engine, preset) => {
    pushUndo();
    const t = newTrack(type, name, engine, preset);
    S.song.tracks.push(t);
    const i = S.song.tracks.length - 1;
    send('addTrack', { index: i, track: t });
    select(i, -1); markDirty();
  };
  sheet('Add a track', [
    { label: 'Synth (808s, pads, leads)', fn: () => add('instrument', 'Synth', 3) },
    { label: 'Drum kit (16 pads)', fn: () => add('drums', 'Drums') },
    { label: 'Chop sampler', fn: () => add('instrument', 'Chops', 0) },
    { label: 'Voice (formant singer)', fn: () => add('instrument', 'Voice', 1) },
    { label: 'Chant (singing head)', fn: () => add('instrument', 'Chant', 5) },
    { label: 'Grain', fn: () => add('instrument', 'Grain', 4) },
    { label: 'Audio (samples, recordings)', fn: () => add('audio', 'Audio') },
  ]);
}
function trackMenu(r) {
  const t = S.song.tracks[r];
  sheet(t.name, [
    { label: 'Rename', fn: () => { const n = prompt('Track name', t.name); if (n) { pushUndo(); t.name = n; markDirty(); draw(); emit('select'); } } },
    { label: 'Sound and keys', fn: () => emit('view', 'play') },
    { label: 'Move up', fn: () => move(r, r - 1) },
    { label: 'Move down', fn: () => move(r, r + 1) },
    { label: 'Delete track', danger: true, fn: () => { if (!confirm(`Delete ${t.name}?`)) return; pushUndo(); S.song.tracks.splice(r, 1); send('removeTrack', { index: r }); select(Math.max(0, r - 1), -1); markDirty(); } },
  ]);
}
function move(a, b) {
  if (b < 0 || b >= S.song.tracks.length) return;
  pushUndo(); const [t] = S.song.tracks.splice(a, 1); S.song.tracks.splice(b, 0, t);
  send('moveTrack', { from: a, to: b }); select(b, -1); markDirty();
}
export function clipMenu() {
  const t = selTrack(), c = t && t.clips[S.clip];
  if (!c) return;
  sheet(c.name || 'Clip', [
    { label: 'Edit notes', fn: openEditor },
    { label: 'Duplicate after', fn: duplicateClip },
    { label: 'Rename', fn: () => { const n = prompt('Clip name', c.name); if (n != null) { pushUndo(); c.name = n; markDirty(); draw(); } } },
    { label: 'Loop this clip', fn: () => { S.song.loopStart = c.start; S.song.loopEnd = c.start + c.length; S.song.loopOn = true; syncSong(); markDirty(); emit('toolbar'); draw(); } },
    { label: 'Delete clip', danger: true, fn: deleteClip },
  ]);
}
export function duplicateClip() {
  const t = selTrack(), c = t && t.clips[S.clip]; if (!c) return;
  pushUndo(); const d = JSON.parse(JSON.stringify(c)); d.start = c.start + c.length; t.clips.push(d);
  S.clip = t.clips.length - 1; syncTrack(S.sel, 'clips'); markDirty(); emit('select'); draw();
}
export function deleteClip() {
  const t = selTrack(); if (!t || S.clip < 0) return;
  pushUndo(); t.clips.splice(S.clip, 1); S.clip = -1; syncTrack(S.sel, 'clips'); markDirty(); emit('select'); draw();
}
function loopMenu() {
  sheet('Loop', [
    { label: S.song.loopOn ? 'Turn loop off' : 'Turn loop on', fn: () => { S.song.loopOn = !S.song.loopOn; syncSong(); markDirty(); emit('toolbar'); draw(); } },
    { label: 'Loop bars 1-4', fn: () => { S.song.loopStart = 0; S.song.loopEnd = 16; S.song.loopOn = true; syncSong(); markDirty(); emit('toolbar'); draw(); } },
  ]);
}
export function arrangeToolbar(tb) {
  const c = S.clip >= 0;
  tb.append(
    h('button', { onclick: addTrackMenu }, '+ Track'),
    h('button', { onclick: undo }, 'Undo'), h('button', { onclick: redo }, 'Redo'),
    h('span', { class: 'sep' }),
    h('button', { class: S.song.loopOn ? 'on' : '', onclick: () => { S.song.loopOn = !S.song.loopOn; syncSong(); markDirty(); emit('toolbar'); draw(); } }, 'Loop'),
    h('button', { class: S.song.metronome ? 'on' : '', onclick: () => { S.song.metronome = !S.song.metronome; syncSong(); emit('toolbar'); } }, 'Click'),
  );
  if (c) tb.append(h('span', { class: 'sep' }), h('button', { onclick: openEditor }, 'Edit'), h('button', { onclick: duplicateClip }, 'Copy'), h('button', { onclick: deleteClip }, 'Delete'));
  else tb.append(h('span', { class: 'label' }, 'Tap a lane to add a clip, drag the ruler to loop'));
}

export function draw() {
  if (!cv || !S.song || S.view !== 'arrange') return;
  const { g, w, h: H } = fitCanvas(cv);
  g.fillStyle = '#0d0b14'; g.fillRect(0, 0, w, H);
  const tracks = S.song.tracks;
  const b0 = Math.floor(view.x), b1 = beatAt(w) + 1;
  // lanes
  g.save(); g.beginPath(); g.rect(HW, RH, w - HW, H - RH); g.clip();
  tracks.forEach((t, r) => {
    const y = RH + r * ROW - view.y;
    if (y > H || y + ROW < RH) return;
    g.fillStyle = r === S.sel ? '#17132a' : (r % 2 ? '#100e18' : '#0d0b14'); g.fillRect(HW, y, w - HW, ROW);
  });
  for (let b = b0; b <= b1; b++) {
    const x = xOf(b); g.fillStyle = b % 4 === 0 ? '#2c2640' : '#1a1626';
    if (b % 4 === 0 || view.ppb > 10) g.fillRect(x, RH, 1, H - RH);
  }
  if (S.song.loopOn) { g.fillStyle = 'rgba(155,107,255,0.07)'; g.fillRect(xOf(S.song.loopStart), RH, (S.song.loopEnd - S.song.loopStart) * view.ppb, H - RH); }
  tracks.forEach((t, r) => {
    const y = RH + r * ROW - view.y;
    if (y > H || y + ROW < RH) return;
    const col = cssColour(t.colour);
    t.clips.forEach((c, i) => {
      const x = xOf(c.start), cw = c.length * view.ppb;
      if (x > w || x + cw < HW) return;
      const sel = r === S.sel && i === S.clip;
      g.fillStyle = col + (sel ? 'ff' : '55'); roundRect(g, x + 1, y + 4, cw - 2, ROW - 8, 6); g.fill();
      if (sel) { g.strokeStyle = '#fff'; g.lineWidth = 1.5; roundRect(g, x + 1, y + 4, cw - 2, ROW - 8, 6); g.stroke(); }
      g.fillStyle = sel ? '#0d0b14' : '#ece8f6'; g.font = '600 11px -apple-system,sans-serif';
      if (cw > 30) g.fillText((c.name || '').slice(0, Math.floor(cw / 7)), x + 6, y + 17);
      if (c.kind === 'midi' && c.notes.length) {
        let lo = 127, hi = 0; for (const n of c.notes) { lo = Math.min(lo, n.pitch); hi = Math.max(hi, n.pitch); }
        const span = Math.max(6, hi - lo + 1), top = y + 22, hh = ROW - 30;
        g.fillStyle = sel ? 'rgba(13,11,20,.8)' : col;
        for (const n of c.notes) { if (n.start >= c.length) continue;
          g.fillRect(x + n.start * view.ppb, top + (1 - (n.pitch - lo + 1) / span) * hh, Math.max(1.5, Math.min(n.length, c.length - n.start) * view.ppb - 1), Math.max(2, hh / span)); }
      } else if (c.kind === 'audio') { g.fillStyle = sel ? 'rgba(13,11,20,.8)' : col; g.font = '11px -apple-system,sans-serif'; g.fillText('♫ ' + (c.file || '').split(/[\\/]/).pop(), x + 6, y + 36); }
      if (sel) { g.fillStyle = '#fff'; g.fillRect(x + cw - 6, y + ROW / 2 - 10, 3, 20); }
    });
  });
  // playhead
  const px = xOf(nowBeat());
  g.fillStyle = '#7be3a5'; g.fillRect(px - 0.5, RH, 1.5, H - RH);
  g.restore();
  // ruler
  g.fillStyle = '#15121f'; g.fillRect(0, 0, w, RH);
  if (S.song.loopOn) { g.fillStyle = 'rgba(155,107,255,0.45)'; g.fillRect(Math.max(HW, xOf(S.song.loopStart)), 4, Math.max(0, xOf(S.song.loopEnd) - Math.max(HW, xOf(S.song.loopStart))), RH - 8); }
  g.font = '11px ui-monospace,Menlo,monospace'; g.fillStyle = '#9a93b0';
  const barStep = view.ppb * 4 < 26 ? 4 : view.ppb * 4 < 48 ? 2 : 1;
  for (let b = Math.floor(b0 / 4) * 4; b <= b1; b += 4) { if ((b / 4) % barStep) continue; const x = xOf(b); if (x >= HW - 2) g.fillText(String(b / 4 + 1), x + 3, 17); }
  g.fillStyle = '#7be3a5'; if (px >= HW) { g.beginPath(); g.moveTo(px - 5, RH - 8); g.lineTo(px + 5, RH - 8); g.lineTo(px, RH); g.fill(); }
  // headers
  g.save(); g.beginPath(); g.rect(0, RH, HW, H - RH); g.clip();
  tracks.forEach((t, r) => {
    const y = RH + r * ROW - view.y;
    if (y > H || y + ROW < RH) return;
    g.fillStyle = r === S.sel ? '#211b36' : '#15121f'; g.fillRect(0, y, HW, ROW);
    g.fillStyle = cssColour(t.colour); g.fillRect(0, y + 6, 4, ROW - 12);
    g.fillStyle = '#ece8f6'; g.font = '600 13px -apple-system,sans-serif'; g.fillText(t.name.slice(0, 11), 12, y + 24);
    g.fillStyle = '#9a93b0'; g.font = '11px -apple-system,sans-serif';
    const kind = t.type === 'drums' ? 'Drums' : t.type === 'audio' ? 'Audio' : t.type === 'plugin' ? 'VST3 (computer)' : ['Chop', 'Voice', 'Chop+Voice', 'Synth', 'Grain', 'Chant'][Math.round(t.inst.values[0])];
    g.fillText(kind + (t.mute ? ' · M' : '') + (t.solo ? ' · S' : ''), 12, y + 41);
    g.fillStyle = '#2c2640'; g.fillRect(0, y + ROW - 1, HW, 1);
  });
  g.restore();
  g.fillStyle = '#2c2640'; g.fillRect(HW - 1, 0, 1, H); g.fillRect(0, RH - 1, w, 1);
  if (!tracks.length) { g.fillStyle = '#9a93b0'; g.font = '14px -apple-system,sans-serif'; g.fillText('Tap + Track to start', HW + 16, RH + 40); }
}
function roundRect(g, x, y, w, hh, r) { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + hh, r); g.arcTo(x + w, y + hh, x, y + hh, r); g.arcTo(x, y + hh, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); }
export { roundRect };
