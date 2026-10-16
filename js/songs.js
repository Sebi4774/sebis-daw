// Sebi's DAW mobile - the Songs screen: songs kept on the phone, open/save through the Files app (iCloud Drive), bounce to WAV.
import { S, on, emit, send, sheet, toast, h, dbAll, dbGet, dbPut, dbDel, saveLocal, songText, loadSongText, templateSong, pushSong, clearUndo,
  addAudioFile, loadStoredAudio, songAudioNames, storedFileBytes, tool, params, audioGet, fileNameOf } from './ctx.js';
import { SongLoader } from './dawcore.js';

export function initSongs() {
  document.getElementById('fileOpen').addEventListener('change', e => { openFiles([...e.target.files]); e.target.value = ''; });
}
const safeName = n => (n || 'Untitled').replace(/[\\/:*?"<>|]+/g, '-').trim() || 'Untitled';

export async function setSong(song, name, id) {
  S.song = song; S.name = name; S.songId = id; S.sel = 0; S.clip = -1; S.dirty = false;
  send('stop'); S.playing = false;
  S.missing = await loadStoredAudio(songAudioNames(song));
  pushSong(); clearUndo();
  emit('song'); emit('dirty');
}
export async function newSong() {
  await setSong(templateSong(), 'New song', null);
  await saveLocal(); renderSongs(); emit('view', 'arrange');
}
async function openStored(id) {
  const rec = await dbGet('songs', id); if (!rec) return;
  try { await setSong(loadSongText(rec.text), rec.name, rec.id); await dbPut('meta', 'last', id); emit('view', 'arrange'); }
  catch (e) { toast('Could not open: ' + e.message); }
}
export async function openLast() {
  const id = await dbGet('meta', 'last');
  if (id && await dbGet('songs', id)) { await openStored(id); return true; }
  return false;
}
// files picked in the Files app: one song plus any samples it uses
async function openFiles(files) {
  const songs = files.filter(f => /\.phsong$/i.test(f.name)), audio = files.filter(f => !/\.phsong$/i.test(f.name));
  let added = 0;
  for (const f of audio) { try { await addAudioFile(f.name, await f.arrayBuffer()); added++; } catch { toast(`${f.name} isn't an audio file this phone can read`); } }
  if (songs.length) {
    const f = songs[0], text = await f.text();
    let song; try { song = loadSongText(text); } catch (e) { toast(e.message); return; }
    const name = f.name.replace(/\.phsong$/i, '');
    const existing = (await dbAll('songs')).find(r => r.name === name);   // same song from the computer: replace the phone copy
    await setSong(song, name, existing ? existing.id : null);
    await saveLocal();
    toast(S.missing.length ? `Opened ${name}. Missing: ${S.missing.join(', ')}` : `Opened ${name}` + (added ? ` with ${added} sample${added > 1 ? 's' : ''}` : ''), 4500);
    emit('view', 'arrange');
  } else if (added) {
    if (S.song) { S.missing = await loadStoredAudio(songAudioNames(S.song)); pushSong(); }
    toast(`Added ${added} sample${added > 1 ? 's' : ''}`);
  }
  renderSongs();
}

// ---------- saving to the Files app ----------
async function shareFiles(files, title) {
  if (navigator.canShare && navigator.canShare({ files })) {
    try { await navigator.share({ files, title }); return true; }
    catch (e) { if (e.name === 'AbortError') return false; }
  }
  for (const f of files) {   // no share sheet (desktop browsers): plain downloads
    const a = document.createElement('a'); a.href = URL.createObjectURL(f); a.download = f.name; document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  }
  return true;
}
export async function saveToFiles(withAllSamples = false) {
  await saveLocal();
  const name = safeName(S.name);
  const files = [new File([songText()], name + '.phsong', { type: 'application/xml' })];
  const sent = (await dbGet('meta', 'exportedFiles')) || {};
  const extra = [];
  for (const n of songAudioNames(S.song)) {
    if (!withAllSamples && sent[n]) continue;
    const bytes = await storedFileBytes(n);
    if (bytes) { files.push(new File([bytes], n)); extra.push(n); }
  }
  const ok = await shareFiles(files, name);
  if (ok) {
    for (const n of extra) sent[n] = Date.now();
    await dbPut('meta', 'exportedFiles', sent);
    S.dirty = false; emit('dirty');
  }
}

// ---------- bounce: the whole song to a WAV, same as the computer's export (44.1 kHz, 2 s tail) ----------
export async function bounce() {
  const song = S.song, end = Math.max(4, ...song.tracks.flatMap(t => t.clips.map(c => c.start + c.length)));
  const x = tool.x;
  const loader = new SongLoader(tool); loader.load({ ...song, loopOn: false, metronome: false }, audioGet);
  const sr = 44100, total = Math.floor((end * 60 / song.bpm + 2) * sr), B = 512;
  const L = new Float32Array(total), R = new Float32Array(total);
  x.pk_seek(tool.s, 0); x.pk_play(tool.s);
  let stopped = false;
  for (let o = 0; o < total; o += B) {
    if (!stopped && x.pk_position(tool.s) >= end) { x.pk_stop(tool.s); stopped = true; }
    const n = Math.min(B, total - o), [l, r] = tool.render(n);
    L.set(l, o); R.set(r, o);
    if ((o / B) % 200 === 0) { toast(`Bouncing… ${Math.round(o / total * 100)}%`, 60000); await new Promise(r => setTimeout(r, 0)); }
  }
  x.pk_clear(tool.s);
  toast('Bounce ready');
  await shareFiles([new File([wav24(L, R, sr)], `${safeName(S.name)} - ${Math.round(song.bpm)} bpm.wav`, { type: 'audio/wav' })], S.name);
}
function wav24(L, R, sr) {
  const n = L.length, buf = new ArrayBuffer(44 + n * 6), v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + n * 6, true); str(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 6, true); v.setUint16(32, 6, true); v.setUint16(34, 24, true); str(36, 'data'); v.setUint32(40, n * 6, true);
  let o = 44;
  for (let i = 0; i < n; i++) for (const s of [L[i], R[i]]) { const x = Math.max(-8388608, Math.min(8388607, Math.round(s * 8388607))); v.setUint8(o, x & 255); v.setUint8(o + 1, (x >> 8) & 255); v.setUint8(o + 2, (x >> 16) & 255); o += 3; }
  return buf;
}

export async function renderSongs() {
  const box = document.getElementById('songs');
  box.innerHTML = '';
  const list = (await dbAll('songs')).sort((a, b) => b.updated - a.updated);
  box.append(...[
    h('h2', {}, 'This song'),
    h('div', { class: 'row' },
      h('button', { class: 'on grow', onclick: () => saveToFiles(false) }, 'Save to Files'),
      h('button', { class: 'grow', onclick: () => document.getElementById('fileOpen').click() }, 'Open from Files')),
    h('div', { class: 'row' },
      h('button', { class: 'grow', onclick: bounce }, 'Bounce'),
      h('button', { class: 'grow', onclick: () => { const n = prompt('Song name', S.name); if (n) { S.name = n; saveLocal(); emit('dirty'); renderSongs(); } } }, 'Rename'),
      h('button', { class: 'grow', onclick: newSong }, 'New song')),
    S.missing.length ? h('p', { class: 'note warn' }, `Missing samples: ${S.missing.join(', ')}. Tap Open from Files and pick them (they sit next to the song on the computer).`) : null,
    h('h2', {}, 'On this phone'),
    h('div', { class: 'list' }, list.map(r => h('div', { class: 'item' + (r.id === S.songId ? ' cur' : ''), onclick: () => openStored(r.id) },
      h('div', { class: 't' }, h('b', {}, r.name), h('small', {}, new Date(r.updated).toLocaleString())),
      h('button', { onclick: e => { e.stopPropagation(); songMenu(r); } }, '⋯')))),
    h('h2', {}, 'Computer and phone'),
    h('p', { class: 'note' }, "Songs save on this phone by themselves. Save to Files sends the song (and any samples you added here) to iCloud Drive: pick the Sebi's DAW folder. On the computer, open that same file in Sebi's DAW. To bring a computer song here, tap Open from Files and pick the .phsong together with its samples. Opening a song with the same name replaces the phone copy."),
    h('p', { class: 'note' }, 'VST3 plugins only play on the computer; they stay in the song. The voices are classic formant synthesis, nothing AI.'),
  ].filter(Boolean));
}
function songMenu(r) {
  sheet(r.name, [
    { label: 'Open', fn: () => openStored(r.id) },
    { label: 'Duplicate', fn: async () => { const id = 'song-' + Date.now().toString(36); await dbPut('songs', id, { ...r, id, name: r.name + ' copy', updated: Date.now() }); renderSongs(); } },
    { label: 'Delete from phone', danger: true, fn: async () => { if (!confirm(`Delete ${r.name} from this phone?`)) return; await dbDel('songs', r.id); if (r.id === S.songId) S.songId = null; renderSongs(); } },
  ]);
}
