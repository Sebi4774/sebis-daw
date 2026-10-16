// Sebi's DAW mobile - the Play screen: keys or pads for the selected track, note recording, and its sound.
import { S, on, emit, send, sheet, toast, h, pushUndo, markDirty, syncTrack, selTrack, P, norm, denorm, params, synthPresets, grainPresets, presetValues,
  addAudioFile, nowBeat, noteName, cssColour, fileNameOf } from './ctx.js';

const ENGINES = ['Chop', 'Voice', 'Both', 'Synth', 'Grain', 'Chant'];
let area, panel, held = new Map(), recNotes = [], recOpen = new Map();

export function initPlay() {
  area = document.getElementById('playArea'); panel = document.getElementById('soundPanel');
  on('select', () => { if (S.view === 'play') renderPlay(); });
  on('recStop', commitRecording);
}

// ---------- live notes and recording ----------
function loopBeat(b) {
  const s = S.song;
  if (s.loopOn && s.loopEnd > s.loopStart + 0.05 && b >= s.loopEnd) return s.loopStart + ((b - s.loopStart) % (s.loopEnd - s.loopStart));
  return b;
}
export function noteOn(pitch, vel) {
  send('live', { track: S.sel, note: pitch, vel, on: true });
  if (S.rec && S.playing) recOpen.set(pitch, { pitch, start: loopBeat(nowBeat()), vel });
}
export function noteOff(pitch) {
  send('live', { track: S.sel, note: pitch, vel: 0, on: false });
  const o = recOpen.get(pitch);
  if (o) { recOpen.delete(pitch); let end = loopBeat(nowBeat()); if (end < o.start) end = S.song.loopEnd; o.length = Math.max(0.05, end - o.start); recNotes.push(o); }
}
function commitRecording() {
  for (const [p, o] of recOpen) { o.length = 0.25; recNotes.push(o); }
  recOpen.clear();
  if (!recNotes.length) return;
  const t = selTrack(); if (!t || t.type === 'audio') { recNotes = []; return; }
  pushUndo();
  const q = S.recQuant ? S.grid : 0;
  const notes = recNotes.map(n => ({ ...n, start: q ? Math.round(n.start / q) * q : n.start, length: t.type === 'drums' ? Math.max(0.25, n.length) : n.length }));
  recNotes = [];
  const lo = Math.min(...notes.map(n => n.start)), hi = Math.max(...notes.map(n => n.start + n.length));
  let ci = t.clips.findIndex(c => c.kind === 'midi' && lo >= c.start && lo < c.start + c.length);
  if (ci < 0) {
    const start = Math.floor(lo / 4) * 4;
    t.clips.push({ attrs: {}, kind: 'midi', name: t.name, start, length: Math.max(4, Math.ceil((hi - start) / 4) * 4), gain: 1, notes: [] });
    ci = t.clips.length - 1;
  }
  const c = t.clips[ci];
  for (const n of notes) c.notes.push({ start: Math.max(0, n.start - c.start), length: n.length, pitch: n.pitch, velocity: Math.round(n.vel * 1000) / 1000 });
  if (hi - c.start > c.length) c.length = Math.ceil((hi - c.start) / 4) * 4;
  c.notes.sort((a, b) => a.start - b.start);
  S.clip = ci; syncTrack(S.sel, 'clips'); markDirty(); emit('select');
  toast(`Recorded ${notes.length} notes into ${c.name || t.name}`);
}

// ---------- keys ----------
function keyboard() {
  const wrap = h('div', {});
  const bar = h('div', { class: 'kb-bar' },
    h('button', { onclick: () => { S.octave = Math.max(0, S.octave - 1); renderPlay(); } }, 'Oct −'),
    h('span', { class: 'grow' }, `${noteName(S.octave * 12 + 12)} – ${noteName(S.octave * 12 + 36)}`),
    h('button', { class: S.recQuant ? 'on' : '', onclick: () => { S.recQuant = !S.recQuant; renderPlay(); } }, 'Rec quantize'),
    h('button', { onclick: () => { S.octave = Math.min(8, S.octave + 1); renderPlay(); } }, 'Oct +'));
  const keys = h('div', { class: 'keys' });
  const base = S.octave * 12 + 12, whites = [];
  for (let n = base; n <= base + 24; n++) if (![1, 3, 6, 8, 10].includes(n % 12)) whites.push(n);
  const ww = 100 / whites.length;
  whites.forEach((n, i) => keys.append(h('div', { 'data-n': n, style: `position:absolute;left:${i * ww}%;width:${ww}%;top:0;bottom:0;background:#e6e1f2;border:1px solid #0d0b14;border-radius:0 0 8px 8px` },
    n % 12 === 0 ? h('span', { style: 'position:absolute;bottom:6px;left:0;right:0;text-align:center;color:#5a527a;font-size:10px' }, noteName(n)) : null)));
  whites.forEach((n, i) => { if ([0, 2, 5, 7, 9].includes(n % 12) && n + 1 <= base + 24)
    keys.append(h('div', { 'data-n': n + 1, style: `position:absolute;left:${(i + 0.68) * ww}%;width:${ww * 0.64}%;top:0;height:60%;background:#1b1727;border:1px solid #0d0b14;border-radius:0 0 6px 6px;z-index:2` })); });
  touchPlay(keys, el => +el.dataset.n, (el, on) => { el.style.filter = on ? 'brightness(0.6) sepia(1) hue-rotate(220deg) saturate(4)' : ''; });
  wrap.append(bar, keys);
  return wrap;
}
function pads() {
  const t = selTrack();
  const grid = h('div', { class: 'pads' });
  for (let row = 3; row >= 0; row--) for (let col = 0; col < 4; col++) {
    const i = row * 4 + col, p = t.pads.find(q => q.i === i);
    grid.append(h('div', { class: 'pad', 'data-n': 36 + i, 'data-i': i }, p ? p.name : `Pad ${i + 1}`));
  }
  touchPlay(grid, el => +el.dataset.n, (el, on) => { el.classList.toggle('hit', on); if (on) { S.lastPad = +el.dataset.i; renderSound(); } });
  return grid;
}
// multi-touch: each finger plays the key under it; slide across keys for glides; height on the key = velocity
function touchPlay(box, noteOf, look) {
  const fingers = new Map();
  const target = e => { const el = document.elementFromPoint(e.clientX, e.clientY); return el && el.closest('[data-n]') && box.contains(el.closest('[data-n]')) ? el.closest('[data-n]') : null; };
  const velOf = (e, el) => { const r = el.getBoundingClientRect(); return Math.max(0.3, Math.min(1, 0.35 + 0.65 * (e.clientY - r.top) / r.height)); };
  box.addEventListener('pointerdown', e => { e.preventDefault(); box.setPointerCapture(e.pointerId); const el = target(e); if (!el) return; fingers.set(e.pointerId, el); look(el, true); noteOn(noteOf(el), velOf(e, el)); });
  box.addEventListener('pointermove', e => {
    if (!fingers.has(e.pointerId)) return; const el = target(e), cur = fingers.get(e.pointerId);
    if (el && el !== cur) { look(cur, false); noteOff(noteOf(cur)); fingers.set(e.pointerId, el); look(el, true); noteOn(noteOf(el), velOf(e, el)); }
  });
  const end = e => { const cur = fingers.get(e.pointerId); if (!cur) return; fingers.delete(e.pointerId); look(cur, false); noteOff(noteOf(cur)); };
  box.addEventListener('pointerup', end); box.addEventListener('pointercancel', end);
}

// ---------- sound controls ----------
function fmtVal(id, v) {
  const p = params[P(id)];
  if (p.type !== 'f') return String(Math.round(v));
  const a = Math.abs(v); return a >= 1000 ? (v / 1000).toFixed(1) + 'k' : a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(2);
}
function knob(t, i, id, label) {
  const v = t.inst.values[P(id)];
  const out = h('output', {}, fmtVal(id, v));
  const input = h('input', { type: 'range', min: 0, max: 1000, value: Math.round(norm(id, v) * 1000), oninput: e => {
    const val = denorm(id, e.target.value / 1000); t.inst.values[P(id)] = val; out.textContent = fmtVal(id, val);
    send('param', { index: i, param: P(id), value: val }); markDirty();
  } });
  return h('div', { class: 'slider' }, h('label', {}, label), input, out);
}
function choice(t, i, id, labels, after) {
  const cur = Math.round(t.inst.values[P(id)]);
  return h('div', { class: 'seg' }, labels.map((l, k) => h('button', { class: k === cur ? 'on' : '', onclick: () => {
    t.inst.values[P(id)] = k; send('param', { index: i, param: P(id), value: k }); markDirty(); if (after) after(); renderSound();
  } }, l)));
}
function toggle(t, i, id, label) {
  const on = t.inst.values[P(id)] > 0.5;
  return h('button', { class: on ? 'on' : '', onclick: () => { t.inst.values[P(id)] = on ? 0 : 1; send('param', { index: i, param: P(id), value: on ? 0 : 1 }); markDirty(); renderSound(); } }, label);
}
function presetPicker(t, i, kind) {
  const list = kind === 0 ? synthPresets : grainPresets;
  const sel = h('select', { onchange: e => {
    const k = +e.target.value; if (k < 0) return;
    t.inst.values = presetValues(t.inst.values, kind, k);
    send('params', { index: i, values: t.inst.values }); markDirty(); toast(list[k].name); renderSound();
  } }, h('option', { value: -1 }, kind === 0 ? 'Load a synth sound…' : 'Load a grain sound…'), list.map((p, k) => h('option', { value: k }, `${p.category}: ${p.name}`)));
  return h('div', { class: 'row' }, sel);
}
function sampleRow(t, i) {
  const name = fileNameOf(t.inst.attrs.sample);
  return h('div', { class: 'row' },
    h('span', { class: 'grow note' }, name ? `Sample: ${name}` : 'No sample loaded'),
    h('button', { onclick: () => emit('pickAudio', async files => {
      const f = files[0]; if (!f) return;
      await addAudioFile(f.name, await f.arrayBuffer());
      pushUndo(); t.inst.attrs.sample = f.name; send('instrument', { index: i, track: t }); markDirty(); renderSound(); toast('Loaded ' + f.name);
    }) }, 'Load sample'));
}

export function renderSound() {
  const t = selTrack(), i = S.sel;
  panel.innerHTML = '';
  if (!t) return;
  panel.append(h('div', { class: 'row' }, h('span', { class: 'swatch', style: `background:${cssColour(t.colour)}` }), h('b', { class: 'grow' }, t.name)));
  if (t.type === 'audio') { panel.append(h('p', { class: 'note' }, 'Audio track: import a file from Arrange (tap an empty spot on this lane). Mixer has its volume.')); return; }
  if (t.type === 'plugin') { panel.append(h('p', { class: 'note' }, 'This track plays a VST3 plugin, which only runs on the computer. Notes still edit here and the plugin is kept in the song.')); return; }
  if (t.type === 'drums') {
    const pi = S.lastPad ?? 0, pad = t.pads.find(q => q.i === pi);
    if (!pad) return;
    const set = (k, v) => { pad[k] = v; send('pads', { index: i, track: { ...t, pads: [pad] } }); markDirty(); };
    const sl = (label, k, lo, hi, step) => { const out = h('output', {}, (+pad[k]).toFixed(2));
      return h('div', { class: 'slider' }, h('label', {}, label), h('input', { type: 'range', min: lo, max: hi, step, value: pad[k], oninput: e => { set(k, +e.target.value); out.textContent = (+e.target.value).toFixed(2); } }), out); };
    panel.append(h('h2', {}, `Pad ${pi + 1}: ${pad.name}`),
      sl('Level', 'gain', 0, 1.5, 0.01), sl('Pitch', 'pitch', -24, 24, 0.5), sl('Length', 'decay', 0.02, 1, 0.01), sl('Drive', 'drive', 0, 1, 0.01), sl('Pan', 'pan', -1, 1, 0.01),
      h('div', { class: 'row' },
        h('button', { class: pad.reverse ? 'on' : '', onclick: () => { set('reverse', !pad.reverse); renderSound(); } }, 'Reverse'),
        h('select', { onchange: e => set('choke', +e.target.value) }, [0, 1, 2, 3, 4].map(k => h('option', { value: k, selected: pad.choke === k }, k ? `Choke group ${k}` : 'No choke'))),
        h('button', { onclick: () => emit('pickAudio', async files => {
          const f = files[0]; if (!f) return;
          await addAudioFile(f.name, await f.arrayBuffer());
          pushUndo(); pad.file = f.name; pad.name = f.name.replace(/\.[^.]+$/, ''); send('pads', { index: i, track: { ...t, pads: [pad] } }); markDirty(); renderPlay(); toast('Pad ' + (pi + 1) + ': ' + f.name);
        }) }, 'Load sample'),
        pad.file ? h('button', { onclick: () => { pushUndo(); pad.file = ''; pad.wasSample = true; pad.name = 'Pad ' + (pi + 1); send('pads', { index: i, track: { ...t, pads: [pad] } }); markDirty(); renderPlay(); } }, 'Built-in sound') : null),
      h('p', { class: 'note' }, 'Hit a pad to pick it. Pads sit on C2 and up, the same as on the computer.'));
    return;
  }
  const eng = Math.round(t.inst.values[P('engine')]);
  panel.append(h('h2', {}, 'Engine'), choice(t, i, 'engine', ENGINES));
  if (eng === 3) {
    panel.append(h('h2', {}, 'Sound'), presetPicker(t, i, 0),
      knob(t, i, 'mTone', 'Tone'), knob(t, i, 'mAttack', 'Attack'), knob(t, i, 'mRelease', 'Release'), knob(t, i, 'mWidth', 'Width'),
      knob(t, i, 'glide', 'Glide'), knob(t, i, 'sVib', 'Vibrato'), knob(t, i, 'blend', 'Blend A/B'),
      h('div', { class: 'row' }, toggle(t, i, 'sMono', 'Mono'), toggle(t, i, 'legato', 'Legato')),
      h('h2', {}, 'Oscillator A'), choice(t, i, 'aOsc', ['Analog', 'Wavetable', 'FM', 'Noise', 'Sample']), knob(t, i, 'aShape', 'Shape'), knob(t, i, 'aCutoff', 'Cutoff'), knob(t, i, 'aReso', 'Resonance'), knob(t, i, 'aDrive', 'Drive'), knob(t, i, 'aSub', 'Sub'),
      h('p', { class: 'note' }, 'Layer B, LFOs and zones are on the computer; the phone keeps whatever they are set to.'));
    if (Math.round(t.inst.values[P('aOsc')]) === 4 || Math.round(t.inst.values[P('bOsc')]) === 4) panel.append(sampleRow(t, i));
  }
  if (eng === 0 || eng === 2) {
    panel.append(h('h2', {}, 'Chop'), sampleRow(t, i), choice(t, i, 'playMode', ['Chromatic', 'Slice', 'Motion']));
    if (Math.round(t.inst.values[P('playMode')]) === 2) panel.append(choice(t, i, 'rate', ['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32']),
      knob(t, i, 'chance', 'Chance'), knob(t, i, 'jitter', 'Jitter'), knob(t, i, 'stutter', 'Stutter'), knob(t, i, 'revChance', 'Reverse'), knob(t, i, 'pitchSpread', 'Pitch jumps'), knob(t, i, 'tape', 'Tape speed'), knob(t, i, 'swing', 'Swing'), knob(t, i, 'seed', 'Pattern'));
    panel.append(knob(t, i, 'transpose', 'Pitch'), knob(t, i, 'lofi', 'Lo-fi'), knob(t, i, 'cutoff', 'Filter'), knob(t, i, 'attack', 'Attack'), knob(t, i, 'release', 'Release'), knob(t, i, 'cloud', 'Cloud'),
      h('div', { class: 'row' }, toggle(t, i, 'reverse', 'Reverse'), toggle(t, i, 'mono', 'Mono glide')));
  }
  if (eng === 1 || eng === 2) {
    const ta = h('textarea', { placeholder: 'Words to sing, one syllable per note', oninput: e => { t.inst.attrs.lyrics = e.target.value; send('lyrics', { index: i, text: e.target.value }); markDirty(); } });
    ta.value = t.inst.attrs.lyrics || '';
    panel.append(h('h2', {}, 'Voice (formant, no AI)'), ta,
      choice(t, i, 'language', ['English', 'Japanese']), h('div', { style: 'height:6px' }), choice(t, i, 'role', ['Hook', 'Ad-lib', 'Texture']), h('div', { style: 'height:6px' }),
      choice(t, i, 'effort', ['Normal', 'Breathy', 'Whispered']), h('div', { style: 'height:6px' }), choice(t, i, 'pitchStyle', ['Sung', 'Natural', 'Monotone']),
      knob(t, i, 'formant', 'Formant'), knob(t, i, 'breath', 'Breath'), knob(t, i, 'buzz', 'Buzz'), knob(t, i, 'vibrato', 'Vibrato'), knob(t, i, 'voiceGlide', 'Glide'), knob(t, i, 'speed', 'Consonants'),
      h('div', { class: 'row' }, toggle(t, i, 'robot', 'Robot pitch'), toggle(t, i, 'chip', 'Speech chip')));
  }
  if (eng === 4) {
    panel.append(h('h2', {}, 'Grain'), sampleRow(t, i), presetPicker(t, i, 1), choice(t, i, 'gMode', ['Hold', 'Step', 'Keys']),
      knob(t, i, 'gPos', 'Position'), knob(t, i, 'gLength', 'Length'), knob(t, i, 'gRate', 'Grains'), knob(t, i, 'gSpray', 'Spray'), knob(t, i, 'gScan', 'Scan'),
      knob(t, i, 'gTune', 'Tune'), knob(t, i, 'gPitchRand', 'Scatter'), knob(t, i, 'gRev', 'Reverse'), knob(t, i, 'gHaze', 'Haze'), knob(t, i, 'gSpace', 'Space'),
      choice(t, i, 'gShape', ['Hann', 'Gauss', 'Tri', 'Trap', 'Square', 'Pluck', 'Swell']));
  }
  if (eng === 5) {
    panel.append(h('h2', {}, 'Chant (singing head)'), knob(t, i, 'chVowel', 'Vowel'), knob(t, i, 'chHead', 'Head size'), knob(t, i, 'chOpen', 'Mouth sweep'), knob(t, i, 'chSweep', 'Sweep time'),
      knob(t, i, 'chGlide', 'Glide'), knob(t, i, 'chVib', 'Vibrato'), knob(t, i, 'chChoir', 'Choir'), knob(t, i, 'chEcho', 'Echo'), knob(t, i, 'chGrit', 'Grit'));
  }
  panel.append(h('h2', {}, 'FX'), knob(t, i, 'dirt', 'DIRT'), knob(t, i, 'space', 'Space'), knob(t, i, 'delayMix', 'Delay'), knob(t, i, 'chorus', 'Chorus'), knob(t, i, 'tilt', 'Tone tilt'), knob(t, i, 'output', 'Output'));
}

export function renderPlay() {
  const t = selTrack();
  area.innerHTML = '';
  if (!t) { panel.innerHTML = ''; return; }
  if (t.type === 'drums') area.append(pads());
  else if (t.type !== 'audio') area.append(keyboard());
  renderSound();
}
export function playToolbar(tb) {
  const t = selTrack();
  tb.append(h('select', { onchange: e => { S.sel = +e.target.value; S.clip = -1; emit('select'); renderPlay(); } },
    S.song.tracks.map((x, k) => h('option', { value: k, selected: k === S.sel }, x.name))),
    h('span', { class: 'label' }, S.rec ? 'Recording: press Play and play' : 'Press ● then ▶ to record notes'));
}
