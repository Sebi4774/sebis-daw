// Sebi's DAW mobile - talks to the C++ sound engine (dawcore.wasm). Works in the page, in the audio thread and in Node.

function coreImports(getMem) {
  return {
    env: { emscripten_notify_memory_growth: () => {} },
    wasi_snapshot_preview1: {
      random_get: (ptr, len) => { const u = new Uint8Array(getMem().buffer, ptr, len); for (let i = 0; i < len; i++) u[i] = (Math.random() * 256) | 0; return 0; },
      fd_close: () => 0, fd_write: () => 0, fd_seek: () => 0,
    },
  };
}
export async function instantiateCore(bytesOrModule) {
  let mem = null;
  const imports = coreImports(() => mem);
  const inst = bytesOrModule instanceof WebAssembly.Module
    ? await WebAssembly.instantiate(bytesOrModule, imports)
    : (await WebAssembly.instantiate(bytesOrModule, imports)).instance;
  mem = inst.exports.memory;
  if (inst.exports._initialize) inst.exports._initialize();
  return new Core(inst.exports);
}
// the audio thread compiles it synchronously (small module, allowed off the main thread)
export function instantiateCoreSync(bytes) {
  let mem = null;
  const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), coreImports(() => mem));
  mem = inst.exports.memory;
  if (inst.exports._initialize) inst.exports._initialize();
  return new Core(inst.exports);
}

// UTF-8 by hand: the audio thread has no TextEncoder/TextDecoder
export function utf8Encode(str) {
  const out = [];
  for (const ch of str) {
    let c = ch.codePointAt(0);
    if (c < 0x80) out.push(c);
    else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
  }
  return new Uint8Array(out);
}
export function utf8Decode(u) {
  let s = '';
  for (let i = 0; i < u.length;) {
    const b = u[i++];
    let c = b;
    if (b >= 0xf0) { c = ((b & 7) << 18) | ((u[i++] & 63) << 12) | ((u[i++] & 63) << 6) | (u[i++] & 63); }
    else if (b >= 0xe0) { c = ((b & 15) << 12) | ((u[i++] & 63) << 6) | (u[i++] & 63); }
    else if (b >= 0xc0) { c = ((b & 31) << 6) | (u[i++] & 63); }
    s += String.fromCodePoint(c);
  }
  return s;
}

export class Core {
  constructor(x) { this.x = x; this.s = 0; }
  get mem() { return this.x.memory.buffer; }
  str(ptr) { const u = new Uint8Array(this.mem); let e = ptr; while (u[e]) e++; return utf8Decode(u.subarray(ptr, e)); }
  withString(s, fn) {
    const b = utf8Encode(s + '\0'); const p = this.x.pk_alloc(b.length);
    new Uint8Array(this.mem).set(b, p); try { return fn(p); } finally { this.x.pk_free(p); }
  }
  withFloats(arr, fn) {
    if (!arr) return fn(0);
    const p = this.x.pk_alloc(arr.length * 4 || 4);
    new Float32Array(this.mem, p, arr.length).set(arr); try { return fn(p); } finally { this.x.pk_free(p); }
  }
  withDoubles(arr, fn) {
    const p = this.x.pk_alloc(arr.length * 8 || 8);
    new Float64Array(this.mem, p, arr.length).set(arr); try { return fn(p); } finally { this.x.pk_free(p); }
  }
  // the knob table (ids, ranges, defaults), same as the desktop instrument
  params() {
    if (this._params) return this._params;
    const x = this.x, n = x.pk_param_count(), out = [];
    for (let i = 0; i < n; i++) out.push({ id: this.str(x.pk_param_id(i)), type: String.fromCharCode(x.pk_param_type(i)), lo: x.pk_param_lo(i), hi: x.pk_param_hi(i), def: x.pk_param_def(i) });
    return (this._params = out);
  }
  presets(kind) {
    const x = this.x, out = [];
    for (let i = 0; i < x.pk_preset_count(kind); i++) out.push({ name: this.str(x.pk_preset_name(kind, i)), category: this.str(x.pk_preset_category(kind, i)) });
    return out;
  }
  create(sampleRate) { this.s = this.x.pk_new(sampleRate); this.sr = sampleRate; this.bufL = 0; this.cap = 0; return this; }
  render(n, live = false) {
    if (n > this.cap) { if (this.bufL) { this.x.pk_free(this.bufL); this.x.pk_free(this.bufR); } this.bufL = this.x.pk_alloc(n * 4); this.bufR = this.x.pk_alloc(n * 4); this.cap = n; }
    this.x.pk_process(this.s, this.bufL, this.bufR, n, live ? 1 : 0);
    return [new Float32Array(this.mem, this.bufL, n), new Float32Array(this.mem, this.bufR, n)];
  }
  addSource(l, r, rate) {
    return this.withFloats(l, pl => this.withFloats(r, pr => this.x.pk_source_add(this.s, pl, pr, l.length, rate)));
  }
  sourceData(id) {
    const n = this.x.pk_source_length(this.s, id);
    return [new Float32Array(this.mem, this.x.pk_source_data(this.s, id, 0), n).slice(), new Float32Array(this.mem, this.x.pk_source_data(this.s, id, 1), n).slice()];
  }
}

// ---- pushing a song (the model from songfile.js) into a core ----
// audio: { get(fileName) -> {l: Float32Array, r: Float32Array, rate} | undefined }
export function clipsData(track) {
  const d = [0];
  for (const c of track.clips) {
    if (c.kind !== 'midi') continue;
    d[0]++;
    d.push(c.start, c.length, c.notes.length);
    for (const n of c.notes) d.push(n.start, n.length, n.pitch, n.velocity);
  }
  return d;
}
export const TYPE = { instrument: 0, drums: 1, audio: 2, plugin: 3 };
export const fileNameOf = p => String(p || '').split(/[\\/]/).pop();

export class SongLoader {
  constructor(core) { this.core = core; this.sourceIds = new Map(); this.stretchIds = new Map(); }
  sourceFor(name, audio) {
    if (this.sourceIds.has(name)) return this.sourceIds.get(name);
    const a = audio.get(name);
    const id = a ? this.core.addSource(a.l, a.r || a.l, a.rate) : -1;
    this.sourceIds.set(name, id);
    return id;
  }
  stretched(srcId, speed, semis) {
    const key = `${srcId}|${speed.toFixed(5)}|${semis.toFixed(2)}`;
    if (!this.stretchIds.has(key)) this.stretchIds.set(key, this.core.x.pk_stretch(this.core.s, srcId, speed, semis));
    return this.stretchIds.get(key);
  }
  song(song) {
    this.core.x.pk_song(this.core.s, song.bpm, song.master, song.limiter ? 1 : 0, song.loopOn ? 1 : 0, song.loopStart, song.loopEnd, song.metronome ? 1 : 0);
  }
  mix(i, t) { this.core.x.pk_track_mix(this.core.s, i, t.volume, t.pan, t.drive, t.mute ? 1 : 0, t.solo ? 1 : 0); }
  instrument(i, t, audio) {
    const x = this.core.x, s = this.core.s;
    this.core.withFloats(t.inst.values, p => x.pk_track_params(s, i, p, t.inst.values.length));
    this.core.withString(t.inst.attrs.lyrics || '', p => x.pk_track_lyrics(s, i, p));
    const sample = fileNameOf(t.inst.attrs.sample);
    if (sample && audio.get(sample)) {
      const a = audio.get(sample);
      const mono = new Float32Array(a.l.length);
      const r = a.r || a.l;
      for (let k = 0; k < mono.length; k++) mono[k] = (a.l[k] + r[k]) / 2;
      this.core.withFloats(mono, p => x.pk_track_sample(s, i, p, mono.length, a.rate));
    }
  }
  pads(i, t, audio) {
    const x = this.core.x, s = this.core.s;
    for (const p of t.pads) {
      const a = p.file ? audio.get(fileNameOf(p.file)) : null;
      if (a) this.core.withFloats(a.l, pl => this.core.withFloats(a.r, pr => this.core.withString(p.file, pf => this.core.withString(p.name, pn =>
        x.pk_pad_sample(s, i, p.i, pl, a.r ? pr : 0, a.l.length, a.rate, pf, pn)))));
      else if (p.file || p.wasSample) x.pk_pad_factory(s, i, p.i);   // missing file: the built-in sound, like the computer
      x.pk_pad(s, i, p.i, p.gain, p.pan, p.pitch, p.decay, p.drive, p.reverse ? 1 : 0, p.choke);
    }
  }
  clips(i, t) { const d = clipsData(t); this.core.withDoubles(d, p => this.core.x.pk_track_clips(this.core.s, i, p, d.length)); }
  audioClips(i, t, audio, bpm) {
    const rows = [];
    for (const c of t.clips) {
      if (c.kind !== 'audio') continue;
      const src = this.sourceFor(fileNameOf(c.file), audio);
      let rendered = -1, speed = c.stretch ? bpm / c.sourceBpm : 1;
      if (src >= 0 && c.stretch && (Math.abs(speed - 1) > 1e-4 || c.transpose !== 0)) rendered = this.stretched(src, speed, c.transpose);
      rows.push(c.start, c.length, src, c.offset, c.gain, c.stretch ? 1 : 0, c.sourceBpm, c.transpose, rendered, speed, c.transpose);
    }
    this.core.withDoubles(rows, p => this.core.x.pk_track_audio(this.core.s, i, p, rows.length / 11));
  }
  lanes(i, t) {
    const x = this.core.x, s = this.core.s;
    x.pk_track_lanes_clear(s, i);
    for (const l of t.lanes) {
      const pts = l.points.flatMap(p => [p.beat, p.value]);
      this.core.withString(l.target, pt => this.core.withDoubles(pts, pp => x.pk_track_lane(s, i, pt, pp, l.points.length)));
    }
  }
  track(i, t, audio, bpm) {
    this.mix(i, t);
    if (t.inst) this.instrument(i, t, audio);
    if (t.type === 'drums') this.pads(i, t, audio);
    if (t.type === 'audio') this.audioClips(i, t, audio, bpm); else this.clips(i, t);
    this.lanes(i, t);
  }
  load(song, audio) {
    const x = this.core.x, s = this.core.s;
    x.pk_clear(s); this.sourceIds.clear(); this.stretchIds.clear();
    this.song(song);
    song.tracks.forEach((t, i) => { x.pk_add_track(s, TYPE[t.type]); this.track(i, t, audio, song.bpm); });
  }
}
