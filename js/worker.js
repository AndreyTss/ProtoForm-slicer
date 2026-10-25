// Фоновый поток движка. Грузит engine.mjs/engine.wasm и профили при первой
// задаче и выполняет задачи по одной: нарезка синхронная, поток занят до конца.
//
// Сообщения от slicer.js:  { id, type: 'measure' | 'printTime', base, profiles, input }
// Ответы:                  { id, kind: 'progress', stage, percent }
//                          { id, kind: 'done', result }
//                          { id, kind: 'error', code, message }
import { pickProfiles } from './profiles.js';

let ready = null; // Promise<{ engine, profiles }>

// Доля шкалы, которую занимает загрузка движка, когда он грузится в этой задаче.
const DOWNLOAD_SHARE = 15;

/**
 * Скачивает и компилирует движок одним потоком: браузер компилирует модуль, пока
 * он ещё качается, а не после. Доля скачанного идёт в onFraction.
 */
async function instantiateStreaming(url, imports, onFraction) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  const total = Number(res.headers.get('content-length')) || 0;
  let body = res.body;
  if (body && total) {
    let got = 0;
    body = body.pipeThrough(new TransformStream({
      transform(chunk, ctrl) {
        got += chunk.length;
        onFraction(Math.min(got / total, 1));
        ctrl.enqueue(chunk);
      },
    }));
  }
  const wasmRes = new Response(body, { headers: { 'Content-Type': 'application/wasm' } });
  if (WebAssembly.instantiateStreaming) return WebAssembly.instantiateStreaming(wasmRes, imports);
  return WebAssembly.instantiate(await wasmRes.arrayBuffer(), imports);
}

function load(base, profilesUrl, report) {
  if (ready) return ready;
  ready = (async () => {
    report('download', 0);
    const [profiles, mod] = await Promise.all([
      fetch(profilesUrl).then((r) => {
        if (!r.ok) throw new Error(`${profilesUrl}: HTTP ${r.status}`);
        return r.json();
      }),
      import(/* @vite-ignore */ base + 'engine.mjs'),
    ]);
    // Ошибку своего загрузчика модуль Emscripten не пробрасывает: ловим её отдельно.
    let failLoad;
    const loadFailed = new Promise((_, reject) => (failLoad = reject));
    const engine = await Promise.race([
      loadFailed,
      mod.default({
        // Свой загрузчик вместо встроенного: ход скачивания и компиляция на лету.
        instantiateWasm(imports, done) {
          instantiateStreaming(base + 'engine.wasm', imports, (f) => report('download', Math.floor(f * (DOWNLOAD_SHARE - 1))))
            .then((r) => done(r.instance, r.module), failLoad);
          return {};
        },
        locateFile: (path) => base + path,
        print: () => {},
        printErr: () => {},
      }),
    ]);
    report('download', DOWNLOAD_SHARE);
    return { engine, profiles };
  })();
  // Неудачную загрузку не запоминаем: следующая задача попробует снова.
  ready.catch(() => { ready = null; });
  return ready;
}

function looksLikeOutOfMemory(message) {
  return /out of memory|OOM|Cannot enlarge memory|could not allocate memory|Array buffer allocation failed/i.test(message);
}

self.onmessage = async (event) => {
  const { id, type, base, profiles: profilesUrl, input } = event.data;
  let loadedHere = false;
  let last = -1;
  const report = (stage, percent) => {
    const p = Math.max(last, Math.min(100, Math.round(percent)));
    last = p;
    self.postMessage({ id, kind: 'progress', stage, percent: p });
  };
  try {
    loadedHere = !ready;
    const { engine, profiles } = await load(base, profilesUrl, report);
    const scale = (p) => (loadedHere ? DOWNLOAD_SHARE + (p * (100 - DOWNLOAD_SHARE)) / 100 : p);
    const set = pickProfiles(profiles, input);
    const args = {
      data: new Uint8Array(input.data),
      fileName: String(input.name ?? 'model.stl'),
      scale: Number(input.scale ?? 1),
      machine: JSON.stringify(set.machine),
      process: JSON.stringify(set.process),
      filament: JSON.stringify(set.filament),
    };
    const onProgress = (stage, percent) => report(stage, scale(percent));
    const text = type === 'printTime' ? engine.printTime(args, onProgress) : engine.measure(args, onProgress);
    const out = JSON.parse(text);
    if (out.error) {
      self.postMessage({ id, kind: 'error', code: out.error.code, message: out.error.message });
      return;
    }
    self.postMessage({ id, kind: 'done', result: out.ok });
  } catch (err) {
    const message = String((err && err.message) || err);
    const code = looksLikeOutOfMemory(message) ? 'out_of_memory' : 'slice_failed';
    self.postMessage({ id, kind: 'error', code, message });
  }
};
