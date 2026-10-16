// Sebi's DAW mobile - shared state, the engine link, storage and small helpers used by every screen.
import { instantiateCore, SongLoader, fileNameOf } from './dawcore.js';
import { parseSong, writeSong, TRACK_COLOURS } from './songfile.js';

export const S = {
  song: null, songId: null, name: 'Untitled', dirty: false,
  sel: 0, clip: -1,            // selected track, selected clip on it
  view: 'arrange', playing: false, pos: 0, posTime: 0, rec: false,
  grid: 0.25, noteLen: 0.25, vel: 0.8, octave: 4,
  missing: [],
};
const listeners = {};
export const on = (ev, fn) => (listeners[ev] ||= []).push(fn);
export const emit = (ev, a) => (listeners[ev] || []).forEach(f => f(a));

// ---------- the main-thread engine (knob table, presets, bounces) ----------
export let tool = null, params = [], paramIndex = {}, synthPresets = [], grainPresets = [], wasmBytes = null;
export async function initTool() {
  wasmBytes = await (await fetch('dawcore.wasm')).arrayBuffer();
  tool = (await instantiateCore(wasmBytes.slice(0))).create(44100);
  params = tool.params();
  params.forEach((p, i) => (paramIndex[p.id] = i));
  synthPresets = tool.presets(0); grainPresets = tool.presets(1);
}
export const P = id => paramIndex[id];
export const norm = (id, v) => tool.x.pk_param_to_norm(P(id), v);
export const denorm = (id, p) => tool.x.pk_param_from_norm(P(id), p);
export function defaults() { return params.map(p => p.def); }
// preset values computed by the engine, the same way the desktop applies them
export function presetValues(values, kind, index) {
  const x = tool.x, s = tool.s;
  x.pk_clear(s); x.pk_add_track(s, 0);
  tool.withFloats(values, p => x.pk_track_params(s, 0, p, values.length));
  x.pk_preset_apply(s, 0, kind, index);
  return params.map((_, i) => x.pk_track_param_get(s, 0, i));
}
export function factoryPads() {
  const x = tool.x, s = tool.s;
  x.pk_clear(s); x.pk_add_track(s, 1);
  const pads = [];
  for (let i = 0; i < 16; i++) pads.push({ attrs: {}, i, name: tool.str(x.pk_pad_name(s, 0, i)), file: '', gain: x.pk_pad_value(s, 0, i, 0), pan: x.pk_pad_value(s, 0, i, 1),
    pitch: x.pk_pad_value(s, 0, i, 2), decay: x.pk_pad_value(s, 0, i, 3), drive: x.pk_pad_value(s, 0, i, 4), reverse: !!x.pk_pad_value(s, 0, i, 5), choke: x.pk_pad_value(s, 0, i, 6) });
  return pads;
}

// ---------- the audio thread ----------
export let ac = null, node = null;
export function send(op, data = {}) { if (node) node.port.postMessage({ op, ...data }); }
export async function startAudio() {
  ac = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch {}   // iPhone: play even with the ring/silent switch on silent
  await ac.audioWorklet.addModule('worklet.js');
  node = new AudioWorkletNode(ac, 'daw', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm: wasmBytes } });
  node.connect(ac.destination);
  node.port.onmessage = e => {
    const m = e.data;
    if (m.op === 'tick') { S.pos = m.pos; S.posTime = performance.now(); S.engPlaying = !!m.playing; S.lastMaster = m.master; emit('tick', m); }
  };
  await ac.resume();
}
// the beat right now, between ticks
export function nowBeat() {
  if (!S.playing) return S.pos;
  return S.pos + (performance.now() - S.posTime) / 1000 * S.song.bpm / 60;
}

// ---------- audio files the songs use (kept on the phone, by file name) ----------
const audio = new Map();   // name -> {l, r, rate}
export const audioGet = { get: n => audio.get(n) };
export function hasAudio(name) { return audio.has(name); }
export async function decodeAudio(bytes) {
  const buf = await ac.decodeAudioData(bytes.slice(0));
  const l = buf.getChannelData(0).slice(), r = buf.numberOfChannels > 1 ? buf.getChannelData(1).slice() : null;
  return { l, r, rate: buf.sampleRate };
}
export async function addAudioFile(name, bytes, store = true) {
  const d = await decodeAudio(bytes);
  audio.set(name, d);
  send('audio', { files: { [name]: d } });
  if (store) await dbPut('files', name, { name, bytes, added: Date.now() });
  return d;
}
export async function loadStoredAudio(names) {
  const missing = [];
  for (const n of names) {
    if (!n || audio.has(n)) continue;
    const rec = await dbGet('files', n);
    if (rec) { try { await addAudioFile(n, rec.bytes, false); } catch { missing.push(n); } }
    else missing.push(n);
  }
  return missing;
}
export function songAudioNames(song) {
  const names = new Set();
  for (const t of song.tracks) {
    if (t.inst && t.inst.attrs.sample) names.add(fileNameOf(t.inst.attrs.sample));
    for (const p of t.pads) if (p.file) names.add(fileNameOf(p.file));
    for (const c of t.clips) if (c.kind === 'audio' && c.file) names.add(fileNameOf(c.file));
  }
  names.delete('');
  return [...names];
}
export async function storedFileBytes(name) { const r = await dbGet('files', name); return r && r.bytes; }

// ---------- IndexedDB: songs and audio files live on the phone between visits ----------
let dbp = null;
function db() {
  return dbp ||= new Promise((res, rej) => {
    const r = indexedDB.open('sebis-daw', 1);
    r.onupgradeneeded = () => { r.result.createObjectStore('songs'); r.result.createObjectStore('files'); r.result.createObjectStore('meta'); };
    r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
  });
}
const tx = async (store, mode, fn) => { const d = await db(); return new Promise((res, rej) => { const t = d.transaction(store, mode); const q = fn(t.objectStore(store)); t.oncomplete = () => res(q && q.result); t.onerror = () => rej(t.error); }); };
export const dbPut = (store, key, val) => tx(store, 'readwrite', s => s.put(val, key));
export const dbGet = (store, key) => tx(store, 'readonly', s => s.get(key));
export const dbDel = (store, key) => tx(store, 'readwrite', s => s.delete(key));
export const dbAll = store => tx(store, 'readonly', s => s.getAll());

// ---------- the song ----------
export const songText = () => writeSong(S.song, params);
export function trackColour(i) { return TRACK_COLOURS[i % 8]; }
export function cssColour(argb) { return '#' + String(argb || 'ff9b6bff').slice(2); }
export function newTrack(type, name, engine = 3, preset = '') {
  const t = { attrs: {}, type, name, colour: trackColour(S.song ? S.song.tracks.length : 0), volume: 0, pan: 0, drive: 0, mute: false, solo: false, pads: [], clips: [], lanes: [], keep: [] };
  if (type === 'instrument') {
    let values = defaults();
    values[P('engine')] = engine;
    if (preset) { const k = synthPresets.findIndex(p => p.name === preset); if (k >= 0) values = presetValues(values, 0, k); }
    t.inst = { tag: 'PHANTOM', attrs: { lyrics: '', sample: '' }, values, extra: [] };
  }
  if (type === 'drums') t.pads = factoryPads();
  return t;
}
export function templateSong() {
  S.song = { attrs: { version: '2' }, bpm: 140, master: 0, limiter: true, loopStart: 0, loopEnd: 16, loopOn: false, tracks: [] };
  const add = (...a) => { const t = newTrack(...a); t.colour = trackColour(S.song.tracks.length); S.song.tracks.push(t); };
  add('drums', 'Drums'); add('instrument', '808', 3, 'Glide 808'); add('instrument', 'Pad', 3, 'Ice Intro (euphoric)');
  add('instrument', 'Chops', 0); add('instrument', 'Voice', 1);
  return S.song;
}
export function loadSongText(text) { return parseSong(text, params); }
export function pushSong() { send('load', { song: S.song }); }
export function syncTrack(i, what = 'track') { const t = S.song.tracks[i]; if (t) send(what, { index: i, track: t }); }
export function syncSong() { send('song', { song: S.song }); }

// ---------- undo (clips, notes, tracks, mixer) ----------
const undoStack = [], redoStack = [];
const snap = () => JSON.stringify(S.song);
export function pushUndo() { undoStack.push(snap()); if (undoStack.length > 60) undoStack.shift(); redoStack.length = 0; }
function restore(text) { S.song = JSON.parse(text); S.sel = Math.min(S.sel, S.song.tracks.length - 1); S.clip = -1; pushSong(); markDirty(); emit('song'); }
export function undo() { if (!undoStack.length) return toast('Nothing to undo'); redoStack.push(snap()); restore(undoStack.pop()); }
export function redo() { if (!redoStack.length) return toast('Nothing to redo'); undoStack.push(snap()); restore(redoStack.pop()); }
export function clearUndo() { undoStack.length = 0; redoStack.length = 0; }

// ---------- autosave ----------
let saveTimer = 0;
export function markDirty() {
  S.dirty = true; emit('dirty');
  clearTimeout(saveTimer); saveTimer = setTimeout(saveLocal, 800);
}
export async function saveLocal() {
  if (!S.song) return;
  if (!S.songId) S.songId = 'song-' + Date.now().toString(36);
  await dbPut('songs', S.songId, { id: S.songId, name: S.name, text: songText(), updated: Date.now(), exported: S.exported || 0 });
  await dbPut('meta', 'last', S.songId);
}

// ---------- UI helpers ----------
let toastTimer = 0;
export function toast(msg, ms = 2600) {
  const el = document.getElementById('toast'); el.textContent = msg; el.classList.remove('hidden');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.add('hidden'), ms);
}
// bottom sheet: title, then [{label, fn, danger}] or a builder(body) function
export function sheet(title, content) {
  const wrap = document.getElementById('sheet'), body = document.getElementById('sheetBody');
  body.innerHTML = ''; const h = document.createElement('h3'); h.textContent = title; body.appendChild(h);
  if (typeof content === 'function') content(body);
  else {
    const a = document.createElement('div'); a.className = 'actions';
    for (const it of content) { const b = document.createElement('button'); b.textContent = it.label; if (it.danger) b.className = 'danger'; b.onclick = () => { closeSheet(); it.fn(); }; a.appendChild(b); }
    const c = document.createElement('button'); c.textContent = 'Cancel'; c.onclick = closeSheet; a.appendChild(c);
    body.appendChild(a);
  }
  wrap.classList.remove('hidden');
  wrap.onclick = e => { if (e.target === wrap) closeSheet(); };
}
export function closeSheet() { document.getElementById('sheet').classList.add('hidden'); }
export function h(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k.startsWith('on')) e[k] = v; else if (k === 'class') e.className = v; else if (k === 'style') e.style.cssText = v;
    else if (v === true) e.setAttribute(k, ''); else if (v !== false && v != null) e.setAttribute(k, v);
  }
  for (const k of kids.flat()) if (k != null && k !== false) e.append(k.nodeType ? k : document.createTextNode(String(k)));
  return e;
}
export const barText = beat => `${Math.floor(beat / 4) + 1}.${Math.floor(beat % 4) + 1}`;
export const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const noteName = n => NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
export const selTrack = () => S.song && S.song.tracks[S.sel];
export const selClip = () => { const t = selTrack(); return t && S.clip >= 0 ? t.clips[S.clip] : null; };
export { fileNameOf };
