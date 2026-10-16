// Sebi's DAW mobile - reads and writes the desktop's .phsong song files, so one song moves between computer and phone.
// Everything the phone doesn't edit (VST3 plugins, unknown settings) is kept and written back untouched.

// ---------- tiny XML (the desktop writes plain elements with attributes, no text) ----------
const ENT = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" };
function unescapeXml(s) {
  return s.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|[a-z]+);/g, (m, e) =>
    e[0] === '#' ? String.fromCodePoint(e[1] === 'x' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10)) : (ENT[e] ?? m));
}
function escapeXml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
    .replace(/\r/g, '&#13;').replace(/\n/g, '&#10;').replace(/\t/g, '&#9;');
}
export function parseXml(text) {
  let i = 0;
  const root = { tag: '#doc', attrs: {}, children: [] };
  const stack = [root];
  while (i < text.length) {
    const lt = text.indexOf('<', i);
    if (lt < 0) break;
    if (text.startsWith('<?', lt)) { i = text.indexOf('?>', lt) + 2; continue; }
    if (text.startsWith('<!--', lt)) { i = text.indexOf('-->', lt) + 3; continue; }
    if (text.startsWith('<![CDATA[', lt)) { i = text.indexOf(']]>', lt) + 3; continue; }
    if (text.startsWith('<!', lt)) { i = text.indexOf('>', lt) + 1; continue; }
    if (text[lt + 1] === '/') { stack.pop(); i = text.indexOf('>', lt) + 1; continue; }
    const m = /^<([^\s/>]+)/.exec(text.slice(lt, lt + 200));
    const node = { tag: m[1], attrs: {}, children: [] };
    let j = lt + m[0].length;
    const attrRe = /\s*([^\s=/>]+)\s*=\s*("([^"]*)"|'([^']*)')/y;
    for (;;) {
      attrRe.lastIndex = j;
      const a = attrRe.exec(text);
      if (!a) break;
      node.attrs[a[1]] = unescapeXml(a[3] ?? a[4]);
      j = attrRe.lastIndex;
    }
    while (/\s/.test(text[j])) j++;
    stack[stack.length - 1].children.push(node);
    if (text[j] === '/') { i = text.indexOf('>', j) + 1; }
    else { stack.push(node); i = j + 1; }
  }
  return root.children[0];
}
export function writeXml(node, indent = '') {
  const attrs = Object.entries(node.attrs).map(([k, v]) => ` ${k}="${escapeXml(v)}"`).join('');
  if (!node.children.length) return `${indent}<${node.tag}${attrs}/>\n`;
  return `${indent}<${node.tag}${attrs}>\n` + node.children.map(c => writeXml(c, indent + '  ')).join('') + `${indent}</${node.tag}>\n`;
}
const el = (tag, attrs = {}, children = []) => ({ tag, attrs, children });

// ---------- JUCE MemoryBlock base64 (its own alphabet and bit order) + AudioProcessor XML-in-binary ----------
const B64 = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+';
export function juceBase64Decode(s) {
  const dot = s.indexOf('.');
  if (dot <= 0) return null;
  const size = parseInt(s.slice(0, dot), 10);
  if (!(size >= 0)) return null;
  const out = new Uint8Array(size);
  let bit = 0;
  for (let k = dot + 1; k < s.length; k++) {
    const v = B64.indexOf(s[k]);
    if (v < 0) return null;
    for (let b = 0; b < 6; b++, bit++) {
      const byte = bit >> 3;
      if (byte >= size) break;
      if (v & (1 << b)) out[byte] |= 1 << (bit & 7);
    }
  }
  return out;
}
export function juceBase64Encode(bytes) {
  const numChars = Math.floor((bytes.length * 8 + 5) / 6);
  let s = String(bytes.length) + '.';
  for (let i = 0; i < numChars; i++) {
    let v = 0;
    for (let b = 0; b < 6; b++) {
      const bit = i * 6 + b, byte = bit >> 3;
      if (byte < bytes.length && (bytes[byte] >> (bit & 7)) & 1) v |= 1 << b;
    }
    s += B64[v];
  }
  return s;
}
const MAGIC = 0x21324356;
export function xmlFromBinary(bytes) {
  if (!bytes || bytes.length < 9) return null;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (dv.getUint32(0, true) !== MAGIC) return null;
  const len = Math.min(dv.getUint32(4, true), bytes.length - 8);
  let text = new TextDecoder().decode(bytes.subarray(8, 8 + len));
  const z = text.indexOf('\0'); if (z >= 0) text = text.slice(0, z);
  return parseXml(text);
}
export function xmlToBinary(node) {
  const body = new TextEncoder().encode('<?xml version="1.0" encoding="UTF-8"?> ' + writeXml(node).replace(/\n\s*/g, ' ').trim());
  const out = new Uint8Array(8 + body.length + 1);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, MAGIC, true); dv.setUint32(4, body.length, true);
  out.set(body, 8);
  return out;
}

// ---------- the song model ----------
const num = (v, d = 0) => { const x = parseFloat(v); return Number.isFinite(x) ? x : d; };
const bool = v => v != null && (parseInt(v, 10) !== 0 && !Number.isNaN(parseInt(v, 10)) || String(v).trim().toLowerCase() === 'true');
const fmt = x => { const r = Math.round(x * 1e6) / 1e6; return String(Object.is(r, -0) ? 0 : r); };
export const fileNameOf = p => String(p || '').split(/[\\/]/).pop();

export const TRACK_COLOURS = ['ff9b6bff', 'ff4fc3f7', 'ffff7a9c', 'ff7be3a5', 'ffffc857', 'ffff8a4f', 'ff6c8cff', 'ffd77bff'];

// instrument settings: the PHANTOM state tree, knob values pulled out for the app
function readInstrument(b64, paramIds, defaults) {
  const tree = b64 ? xmlFromBinary(juceBase64Decode(b64)) : null;
  const values = defaults.slice();
  const extra = [];
  const node = tree || el('PHANTOM', { lyrics: '', sample: '' });
  for (const c of node.children) {
    if (c.tag === 'PARAM') { const k = paramIds.indexOf(c.attrs.id); if (k >= 0) { values[k] = num(c.attrs.value, defaults[k]); continue; } }
    extra.push(c);
  }
  return { tag: node.tag, attrs: { ...node.attrs }, values, extra };
}
function writeInstrument(inst, paramIds) {
  const kids = paramIds.map((id, k) => el('PARAM', { id, value: fmt(inst.values[k]) }));
  return juceBase64Encode(xmlToBinary(el(inst.tag || 'PHANTOM', { ...inst.attrs }, [...kids, ...inst.extra])));
}

export function newInstrument(defaults, engine = 3) {
  const values = defaults.slice();
  return { tag: 'PHANTOM', attrs: { lyrics: '', sample: '' }, values, extra: [], engineSet: engine };
}

export function parseSong(text, params) {
  const root = parseXml(text);
  if (!root || root.tag !== 'PHANTOMSTUDIO') throw new Error("That isn't a Sebi's DAW song.");
  const ids = params.map(p => p.id), defs = params.map(p => p.def);
  const a = root.attrs;
  const song = {
    attrs: { ...a },
    bpm: num(a.bpm, 140), master: num(a.master, 0), limiter: a.limiter == null ? true : bool(a.limiter),
    loopStart: num(a.loopStart, 0), loopEnd: num(a.loopEnd, 16), loopOn: bool(a.loopOn),
    tracks: [],
  };
  for (const tv of root.children) {
    if (tv.tag !== 'TRACK') continue;
    const t = tv.attrs;
    const track = {
      attrs: { ...t },
      type: ['audio', 'drums', 'plugin'].includes(t.type) ? t.type : 'instrument',
      name: t.name || '', colour: t.colour || TRACK_COLOURS[song.tracks.length % 8],
      volume: num(t.volume), pan: num(t.pan), drive: num(t.drive), mute: bool(t.mute), solo: bool(t.solo),
      pads: [], clips: [], lanes: [], keep: [],
    };
    if (track.type === 'instrument') track.inst = readInstrument(t.phantom, ids, defs);
    for (const c of tv.children) {
      if (c.tag === 'PAD') {
        const p = c.attrs;
        track.pads.push({ attrs: { ...p }, i: Math.round(num(p.i)), name: p.name || '', file: p.file || '',
          gain: num(p.gain, 0.7), pan: num(p.pan), pitch: num(p.pitch), decay: num(p.decay, 1), drive: num(p.drive),
          reverse: bool(p.reverse), choke: Math.round(num(p.choke)) });
      } else if (c.tag === 'CLIPS') {
        for (const cv of c.children) {
          if (cv.tag === 'AUTOMATION') {
            for (const lv of cv.children) {
              if (lv.tag !== 'LANE' || !lv.attrs.target) continue;
              const points = String(lv.attrs.points || '').split(';').filter(Boolean).map(s => s.split(','))
                .filter(p => p.length >= 2).map(p => ({ beat: num(p[0]), value: Math.min(1, Math.max(0, num(p[1]))) }))
                .sort((x, y) => x.beat - y.beat);
              track.lanes.push({ target: lv.attrs.target, points });
            }
            continue;
          }
          if (cv.tag !== 'CLIP') continue;
          const v = cv.attrs;
          const clip = { attrs: { ...v }, kind: v.kind === 'audio' ? 'audio' : 'midi', name: v.name || '',
            start: num(v.start), length: num(v.length, 4), gain: num(v.gain, 1) };
          if (clip.kind === 'audio') {
            clip.file = v.file || ''; clip.offset = num(v.offset); clip.stretch = bool(v.stretch);
            clip.sourceBpm = Math.min(400, Math.max(20, num(v.sourceBpm, song.bpm))); clip.transpose = num(v.transpose);
          } else {
            clip.notes = String(v.notes || '').split(';').filter(Boolean).map(s => s.split(',')).filter(p => p.length >= 4)
              .map(p => ({ start: num(p[0]), length: num(p[1]), pitch: Math.round(num(p[2])), velocity: num(p[3]) }));
          }
          track.clips.push(clip);
        }
      } else track.keep.push(c);   // VST3 plugins and anything newer: kept as they are
    }
    song.tracks.push(track);
  }
  return song;
}

export function writeSong(song, params) {
  const ids = params.map(p => p.id);
  const root = el('PHANTOMSTUDIO', { ...song.attrs, version: song.attrs?.version ?? '2', bpm: fmt(song.bpm), master: fmt(song.master),
    limiter: song.limiter ? '1' : '0', loopStart: fmt(song.loopStart), loopEnd: fmt(song.loopEnd), loopOn: song.loopOn ? '1' : '0' });
  for (const t of song.tracks) {
    const attrs = { ...t.attrs, type: t.type, name: t.name, colour: t.colour, volume: fmt(t.volume), pan: fmt(t.pan), drive: fmt(t.drive),
      mute: t.mute ? '1' : '0', solo: t.solo ? '1' : '0' };
    if (t.inst) attrs.phantom = writeInstrument(t.inst, ids); else delete attrs.phantom;
    const tv = el('TRACK', attrs);
    if (t.type === 'drums')
      for (const p of t.pads) tv.children.push(el('PAD', { ...p.attrs, i: String(p.i), name: p.name, file: p.file, gain: fmt(p.gain), pan: fmt(p.pan),
        pitch: fmt(p.pitch), decay: fmt(p.decay), drive: fmt(p.drive), reverse: p.reverse ? '1' : '0', choke: String(p.choke) }));
    tv.children.push(...t.keep);
    const cl = el('CLIPS');
    for (const c of t.clips) {
      const ca = { ...c.attrs, kind: c.kind, name: c.name, start: fmt(c.start), length: fmt(c.length), gain: fmt(c.gain) };
      if (c.kind === 'audio') Object.assign(ca, { file: c.file, offset: fmt(c.offset), stretch: c.stretch ? '1' : '0', sourceBpm: fmt(c.sourceBpm), transpose: fmt(c.transpose) });
      else ca.notes = c.notes.map(n => `${n.start.toFixed(6)},${n.length.toFixed(6)},${n.pitch},${n.velocity.toFixed(3)};`).join('');
      cl.children.push(el('CLIP', ca));
    }
    cl.children.push(el('AUTOMATION', {}, t.lanes.map(l => el('LANE', { target: l.target,
      points: l.points.map(p => `${p.beat.toFixed(6)},${p.value.toFixed(4)};`).join('') }))));
    tv.children.push(cl);
    root.children.push(tv);
  }
  return '<?xml version="1.0" encoding="UTF-8"?>\n\n' + writeXml(root);
}
