// Sebi's DAW mobile - the Mixer: volume, pan, drive, mute and solo per track, master level and the -1 dB limiter.
import { S, on, send, h, markDirty, syncTrack, syncSong, cssColour, pushUndo } from './ctx.js';

const VLO = -60, VHI = 6, VSKEW = Math.log(0.5) / Math.log((-9 - VLO) / (VHI - VLO));   // the computer's fader feel
const volToNorm = db => Math.pow(Math.min(1, Math.max(0, (db - VLO) / (VHI - VLO))), VSKEW);
const normToVol = p => VLO + (VHI - VLO) * (p > 0 ? Math.exp(Math.log(p) / VSKEW) : 0);
const dbText = db => (db <= -59.5 ? 'off' : (db > 0 ? '+' : '') + db.toFixed(1) + ' dB');
const panText = p => { const v = Math.round(p * 100); return v === 0 ? 'C' : v < 0 ? 'L' + -v : 'R' + v; };
let meters = [], master = null, grOut = null;

export function initMixer() {
  on('tick', m => {
    if (S.view !== 'mix') return;
    meters.forEach((el, i) => { if (el) el.style.width = Math.min(100, Math.sqrt(m.meters[i] || 0) * 100) + '%'; });
    if (master) master.style.width = Math.min(100, Math.sqrt(m.master) * 100) + '%';
    if (grOut) grOut.textContent = m.gr < -0.05 ? `limiting ${m.gr.toFixed(1)} dB` : '';
  });
}
function slider(label, value, min, max, step, text, oninput) {
  const out = h('output', {}, text(value));
  return h('div', { class: 'slider' }, h('label', {}, label), h('input', { type: 'range', min, max, step, value, oninput: e => { out.textContent = text(oninput(+e.target.value)); } }), out);
}
export function renderMixer() {
  const box = document.getElementById('mixer');
  box.innerHTML = ''; meters = [];
  S.song.tracks.forEach((t, i) => {
    const m = h('i'); meters[i] = m;
    const mix = () => { send('mix', { index: i, track: t }); markDirty(); };
    const ms = (cls, key, label) => h('button', { class: `ms ${cls}${t[key] ? ' on' : ''}`, onclick: e => { t[key] = !t[key]; e.target.classList.toggle('on', t[key]); mix(); } }, label);
    box.append(h('div', { class: 'strip' },
      h('div', { class: 'head' }, h('span', { class: 'swatch', style: `background:${cssColour(t.colour)}` }), h('b', {}, t.name), ms('m', 'mute', 'M'), ms('s', 'solo', 'S')),
      h('div', { class: 'meter' }, m),
      slider('Volume', Math.round(volToNorm(t.volume) * 1000), 0, 1000, 1, () => dbText(t.volume), v => { t.volume = Math.round(normToVol(v / 1000) * 10) / 10; mix(); return t.volume; }),
      slider('Pan', t.pan, -1, 1, 0.01, () => panText(t.pan), v => { t.pan = v; mix(); return v; }),
      slider('Drive', t.drive, 0, 1, 0.01, () => Math.round(t.drive * 100) + '%', v => { t.drive = v; mix(); return v; })));
  });
  master = h('i'); grOut = h('small', { class: 'note' });
  box.append(h('div', { class: 'strip' },
    h('div', { class: 'head' }, h('span', { class: 'swatch', style: 'background:#fff' }), h('b', {}, 'Master'), grOut),
    h('div', { class: 'meter' }, master),
    slider('Level', S.song.master, -24, 6, 0.1, () => dbText(S.song.master), v => { S.song.master = v; syncSong(); markDirty(); return v; }),
    h('div', { class: 'row' }, h('button', { class: S.song.limiter ? 'on' : '', onclick: e => { S.song.limiter = !S.song.limiter; e.target.classList.toggle('on', S.song.limiter); syncSong(); markDirty(); } }, 'Limiter −1 dB'),
      h('span', { class: 'note' }, 'Keeps peaks under −1 dB, like on the computer.'))));
}
