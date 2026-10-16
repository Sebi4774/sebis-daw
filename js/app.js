// Sebi's DAW mobile - start-up, the top bar, tabs and toolbar.
import { S, on, emit, send, toast, h, initTool, startAudio, markDirty, syncSong, syncTrack, pushUndo, addAudioFile, selTrack, barText, nowBeat, sheet } from './ctx.js';
import { initArrange, draw as drawArrange, arrangeToolbar, arrangeView } from './arrange.js';
import { initRoll, draw as drawRoll, rollToolbar } from './roll.js';
import { initPlay, renderPlay, playToolbar } from './play.js';
import { initMixer, renderMixer } from './mixer.js';
import { initSongs, renderSongs, newSong, openLast, saveToFiles } from './songs.js';

const $ = id => document.getElementById(id);

function setView(v) {
  S.view = v;
  document.querySelectorAll('.view').forEach(e => e.classList.toggle('on', e.id === 'v-' + v));
  document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.view === v));
  renderToolbar();
  if (v === 'arrange') drawArrange();
  if (v === 'edit') drawRoll();
  if (v === 'play') renderPlay();
  if (v === 'mix') renderMixer();
  if (v === 'songs') renderSongs();
}
function renderToolbar() {
  const tb = $('toolbar'); tb.innerHTML = '';
  if (!S.song) return;
  if (S.view === 'arrange') arrangeToolbar(tb);
  else if (S.view === 'edit') rollToolbar(tb);
  else if (S.view === 'play') playToolbar(tb);
  else if (S.view === 'mix') tb.append(h('span', { class: 'label' }, 'Levels save with the song'));
  else if (S.view === 'songs') tb.append(h('span', { class: 'label' }, 'Songs, sync and export'));
}

function play() {
  if (S.playing) return;
  S.playing = true; S.posTime = performance.now();
  send('play'); $('btnPlay').classList.add('on');
}
function stop() {
  const was = S.playing;
  S.playing = false; send('stop'); $('btnPlay').classList.remove('on');
  if (S.rec) emit('recStop');
  if (!was) { send('seek', { beat: S.song.loopOn ? S.song.loopStart : 0 }); S.pos = S.song.loopOn ? S.song.loopStart : 0; }   // stop twice = back to the start
  drawArrange(); drawRoll();
}

function bpmSheet() {
  sheet('Tempo', body => {
    const input = h('input', { type: 'number', min: 30, max: 300, step: 0.1, value: S.song.bpm, inputmode: 'decimal', style: 'width:120px;font-size:20px' });
    let taps = [];
    const apply = v => { v = Math.min(300, Math.max(30, +v || 140)); pushUndo(); S.song.bpm = Math.round(v * 100) / 100; syncSong();
      S.song.tracks.forEach((t, i) => { if (t.type === 'audio') syncTrack(i, 'clips'); });   // audio clips re-stretch to the new tempo
      $('bpmText').textContent = +S.song.bpm.toFixed(2); input.value = S.song.bpm; markDirty(); };
    body.append(
      h('div', { class: 'row' }, input, h('button', { class: 'on', onclick: () => apply(input.value) }, 'Set')),
      h('div', { class: 'row' }, ...[-5, -1, 1, 5].map(d => h('button', { onclick: () => apply(S.song.bpm + d) }, (d > 0 ? '+' : '') + d)),
        h('button', { onclick: () => { const t = performance.now(); taps = taps.filter(x => t - x < 2500); taps.push(t);
          if (taps.length >= 3) apply(60000 * (taps.length - 1) / (taps[taps.length - 1] - taps[0])); } }, 'Tap')),
      h('p', { class: 'note' }, 'Audio clips that follow the tempo get re-stretched with their pitch kept.'));
  });
}

// audio files onto an audio track at a beat (from tapping an empty audio lane)
function pickFiles(input, fn) { const el = $(input); el.onchange = () => { const f = [...el.files]; el.value = ''; fn(f); }; el.click(); }
on('pickAudio', fn => pickFiles('fileAudio', fn));
on('importAudio', ({ track, beat }) => pickFiles('fileAudio', async files => {
  const t = S.song.tracks[track]; let at = beat;
  for (const f of files) {
    let d; try { d = await addAudioFile(f.name, await f.arrayBuffer()); } catch { toast(`${f.name} isn't an audio file this phone can read`); continue; }
    pushUndo();
    const beats = Math.max(0.25, Math.round(d.l.length / d.rate * S.song.bpm / 60 * 4) / 4);
    t.clips.push({ attrs: {}, kind: 'audio', name: f.name.replace(/\.[^.]+$/, ''), start: at, length: beats, gain: 1, file: f.name, offset: 0, stretch: true, sourceBpm: S.song.bpm, transpose: 0 });
    at += Math.ceil(beats / 4) * 4;
  }
  syncTrack(track, 'clips'); markDirty(); drawArrange();
}));

on('view', setView);
on('select', () => { renderToolbar(); if (S.view === 'edit') drawRoll(); });
on('toolbar', renderToolbar);
on('song', () => { $('bpmText').textContent = +S.song.bpm.toFixed(2); $('songName').textContent = S.name; renderToolbar(); setView(S.view); });
on('dirty', () => { $('dirtyDot').classList.toggle('on', S.dirty); $('songName').textContent = S.name; });
on('tick', () => {
  const b = nowBeat(), bar = Math.floor(b / 4) + 1, beat = Math.floor(b % 4) + 1;
  $('posText').textContent = `${bar}.${beat}`;
  if (S.playing && S.view === 'arrange') {   // keep the playhead on screen
    const w = $('arrCanvas').clientWidth, px = 104 + (b - arrangeView.x) * arrangeView.ppb;
    if (px > w - 30 || px < 104) arrangeView.x = Math.max(0, b - 1);
  }
});

async function boot() {
  await initTool();
  initArrange(); initRoll(); initPlay(); initMixer(); initSongs();
  document.querySelectorAll('#tabs button').forEach(b => b.onclick = () => setView(b.dataset.view));
  $('btnPlay').onclick = play; $('btnStop').onclick = stop;
  $('btnRec').onclick = () => { S.rec = !S.rec; $('btnRec').classList.toggle('on', S.rec); if (!S.rec) emit('recStop'); renderToolbar(); };
  $('btnBpm').onclick = bpmSheet;
  $('btnSongs').onclick = () => setView('songs');
  $('btnStart').onclick = async () => {
    $('btnStart').textContent = 'Starting…';
    try {
      await startAudio();
      if (!(await openLast())) await newSong();
      $('startGate').classList.add('hidden');
      setView('arrange');
    } catch (e) { $('btnStart').textContent = 'Start'; toast('Sound could not start: ' + e.message, 6000); console.error(e); }
  };
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden' && S.song) import('./ctx.js').then(m => m.saveLocal()); });
  if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
  window.__daw = { S, emit };   // for testing
}
boot();
