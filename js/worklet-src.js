// Sebi's DAW mobile - the audio thread: runs the song in the C++ engine, 128 samples at a time.
class DawProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.core = instantiateCoreSync(options.processorOptions.wasm).create(sampleRate);
    this.loader = new SongLoader(this.core);
    this.audio = new Map();
    this.audioGet = { get: n => this.audio.get(n) };
    this.bpm = 140;
    this.frames = 0;
    this.port.onmessage = e => this.handle(e.data);
  }
  handle(m) {
    const L = this.loader, x = this.core.x, s = this.core.s;
    switch (m.op) {
      case 'audio': for (const [k, v] of Object.entries(m.files)) this.audio.set(k, v); break;
      case 'load': this.bpm = m.song.bpm; L.load(m.song, this.audioGet); break;
      case 'song': this.bpm = m.song.bpm; L.song(m.song); break;
      case 'addTrack': x.pk_add_track(s, TYPE[m.track.type]); L.track(m.index, m.track, this.audioGet, this.bpm); break;
      case 'removeTrack': x.pk_remove_track(s, m.index); break;
      case 'moveTrack': x.pk_move_track(s, m.from, m.to); break;
      case 'track': L.track(m.index, m.track, this.audioGet, this.bpm); break;
      case 'mix': L.mix(m.index, m.track); break;
      case 'clips': if (m.track.type === 'audio') L.audioClips(m.index, m.track, this.audioGet, this.bpm); else L.clips(m.index, m.track); break;
      case 'lanes': L.lanes(m.index, m.track); break;
      case 'params': this.core.withFloats(m.values, p => x.pk_track_params(s, m.index, p, m.values.length)); break;
      case 'param': x.pk_track_param(s, m.index, m.param, m.value); break;
      case 'lyrics': this.core.withString(m.text, p => x.pk_track_lyrics(s, m.index, p)); break;
      case 'instrument': L.instrument(m.index, m.track, this.audioGet); break;
      case 'pads': L.pads(m.index, m.track, this.audioGet); break;
      case 'play': x.pk_play(s); break;
      case 'stop': x.pk_stop(s); break;
      case 'seek': x.pk_seek(s, m.beat); break;
      case 'panic': x.pk_panic(s); break;
      case 'live': x.pk_live(s, m.track, m.note, m.vel, m.on ? 1 : 0); break;
    }
  }
  process(inputs, outputs) {
    const out = outputs[0], n = out[0].length;
    const [l, r] = this.core.render(n, true);
    out[0].set(l); if (out[1]) out[1].set(r);
    this.frames += n;
    if (this.frames >= 1600) {   // ~30 times a second: where we are and how loud
      this.frames = 0;
      const x = this.core.x, s = this.core.s, meters = [];
      for (let i = 0; i < 64; i++) meters.push(x.pk_meter(s, i));
      this.port.postMessage({ op: 'tick', pos: x.pk_position(s), playing: x.pk_playing(s), master: x.pk_meter(s, -1), gr: x.pk_limiter_gr(s), meters, time: currentTime });
    }
    return true;
  }
}
registerProcessor('daw', DawProcessor);
