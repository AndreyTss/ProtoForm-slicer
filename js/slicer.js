// Обвязка движка для браузера: фоновый поток, ход работы, отмена, коды ошибок.
//
//   import { createSlicer } from './slicer.js';
//   const slicer = createSlicer({ base: '/engine/' });
//   const r = await slicer.measure({ name, data, scale, layer, strong, material }, (p) => {});
//   const { seconds } = await slicer.printTime({ ... }, (p) => {});
//   slicer.cancel();
//
// В папке base лежат engine.mjs, engine.wasm, worker.js, profiles.js и a1.json.
import { LAYERS, MATERIALS, normalizeLayer } from './profiles.js';

export class SlicerError extends Error {
  constructor(code, message) {
    super(message || code);
    this.name = 'SlicerError';
    this.code = code; // 'bad_file' | 'empty_model' | 'slice_failed' | 'out_of_memory' | 'cancelled'
  }
}

const round = (x, digits) => {
  const k = 10 ** digits;
  return Math.round(x * k) / k;
};

function checkInput(input) {
  if (!input || !(input.data instanceof ArrayBuffer || ArrayBuffer.isView(input.data)))
    throw new TypeError('input.data must be an ArrayBuffer');
  if (!LAYERS.includes(normalizeLayer(input.layer ?? '0.2'))) throw new TypeError(`unknown layer "${input.layer}"`);
  if (!MATERIALS.includes(input.material ?? 'pla')) throw new TypeError(`unknown material "${input.material}"`);
  const scale = Number(input.scale ?? 1);
  if (!(scale > 0) || !Number.isFinite(scale)) throw new TypeError('scale must be a positive number');
}

/** Копия байтов файла: исходный буфер остаётся у вызывающего (он нужен и для printTime). */
function copyBytes(data) {
  const view = ArrayBuffer.isView(data) ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength) : new Uint8Array(data);
  return view.slice().buffer;
}

export function createSlicer({ base = './', profiles = null, workerUrl = null } = {}) {
  const root = base.endsWith('/') ? base : base + '/';
  const absRoot = new URL(root, globalThis.location?.href ?? import.meta.url).href;
  const profilesUrl = new URL(profiles ?? 'a1.json', absRoot).href;
  const workerHref = workerUrl ?? new URL('worker.js', absRoot).href;

  let worker = null;
  let nextId = 1;
  const pending = new Map(); // id → { resolve, reject, onProgress }

  function failAll(code, message) {
    for (const [, job] of pending) job.reject(new SlicerError(code, message));
    pending.clear();
  }

  function ensureWorker() {
    if (worker) return worker;
    worker = new Worker(workerHref, { type: 'module' });
    worker.onmessage = (event) => {
      const msg = event.data;
      const job = pending.get(msg.id);
      if (!job) return;
      if (msg.kind === 'progress') {
        if (job.onProgress) job.onProgress({ stage: msg.stage, percent: msg.percent });
        return;
      }
      pending.delete(msg.id);
      if (msg.kind === 'done') job.resolve(msg.result);
      else job.reject(new SlicerError(msg.code, msg.message));
    };
    worker.onerror = (event) => {
      // Падение потока целиком: чаще всего кончилась память.
      const message = String(event.message || 'engine worker failed');
      const code = /memory|OOM/i.test(message) ? 'out_of_memory' : 'slice_failed';
      event.preventDefault?.();
      worker.terminate();
      worker = null;
      failAll(code, message);
    };
    return worker;
  }

  function run(type, input, onProgress) {
    checkInput(input);
    return new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject, onProgress });
      ensureWorker().postMessage({
        id,
        type,
        base: absRoot,
        profiles: profilesUrl,
        input: {
          name: input.name,
          data: copyBytes(input.data),
          scale: input.scale ?? 1,
          layer: normalizeLayer(input.layer ?? '0.2'),
          strong: Boolean(input.strong),
          material: input.material ?? 'pla',
          settings: input.settings ?? null,
        },
      });
    });
  }

  return {
    async measure(input, onProgress) {
      const r = await run('measure', input, onProgress);
      const grams = r.partGrams + r.supportGrams;
      return {
        grams: round(grams, 2),
        partGrams: round(r.partGrams, 2),
        supportGrams: round(r.supportGrams, 2),
        volumeCm3: round((r.partMm3 + r.supportMm3) / 1000, 3),
        size: r.size.map((x) => round(x, 1)),
        fitsBed: r.fitsBed,
        warnings: r.warnings,
      };
    },

    async printTime(input, onProgress) {
      const r = await run('printTime', input, onProgress);
      return { seconds: Math.round(r.seconds) };
    },

    /** Останавливает текущую работу: поток закрывается, следующий вызов поднимет новый. */
    cancel() {
      if (worker) {
        worker.terminate();
        worker = null;
      }
      failAll('cancelled', 'cancelled');
    },
  };
}
