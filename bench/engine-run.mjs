// Нарезка моделей собранным движком (engine.mjs + engine.wasm) в Node, без фонового потока.
//
// node engine-run.mjs --engine <папка сборки> [--layer 0.2] [--material pla] [--strong]
//                     [--mode measure|printTime|both] [--prepared <папка>] <модель>...
//
// Печатает по строке JSON на модель. С --prepared ещё сохраняет модель после
// автоповорота и расстановки (<имя>.stl) — её режет orca-ref.mjs, чтобы сравнение
// не зависело от того, как Orca на компьютере повернёт деталь.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { pickProfiles } from '../js/profiles.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const flag = (k) => { const i = args.indexOf(k); if (i < 0) return false; args.splice(i, 1); return true; };

const engineDir = opt('--engine', null);
const layer = opt('--layer', '0.2');
const material = opt('--material', 'pla');
const strong = flag('--strong');
const mode = opt('--mode', 'measure');
const preparedDir = opt('--prepared', null);
const profilesPath = opt('--profiles', path.join(here, '../profiles/a1.json'));
const models = args;
if (!engineDir || models.length === 0) {
  console.error('usage: node engine-run.mjs --engine <dir> [--layer 0.2] [--material pla] [--strong] [--mode measure|printTime|both] [--prepared <dir>] <model>...');
  process.exit(2);
}

const profiles = JSON.parse(readFileSync(profilesPath, 'utf8'));
const set = pickProfiles(profiles, { layer, material, strong });
const { default: createEngine } = await import(pathToFileURL(path.join(path.resolve(engineDir), 'engine.mjs')).href);
const t0 = performance.now();
const engine = await createEngine({ print: () => {}, printErr: () => {} });
console.error(`engine ready in ${Math.round(performance.now() - t0)} ms`);
if (preparedDir) mkdirSync(preparedDir, { recursive: true });

const call = (fn, input) => {
  // Время этапа — от его первого сообщения до первого сообщения следующего.
  const stages = {};
  let last = performance.now();
  let current = 'read';
  const out = JSON.parse(fn(input, (stage) => {
    const now = performance.now();
    stages[current] = (stages[current] || 0) + (now - last);
    last = now;
    current = stage;
  }));
  stages[current] = (stages[current] || 0) + (performance.now() - last);
  return { out, stages };
};

for (const file of models) {
  const name = path.basename(file);
  const input = {
    data: new Uint8Array(readFileSync(file)),
    fileName: name,
    scale: 1,
    machine: JSON.stringify(set.machine),
    process: JSON.stringify(set.process),
    filament: JSON.stringify(set.filament),
  };
  const row = { file: name, layer, material, strong };
  try {
    if (preparedDir) {
      const stl = engine.preparedStl(input);
      if (typeof stl === 'string') throw new Error(stl);
      writeFileSync(path.join(preparedDir, name.replace(/\.[^.]+$/, '') + '.stl'), stl);
    }
    if (mode === 'measure' || mode === 'both') {
      const t = performance.now();
      const { out, stages } = call(engine.measure, input);
      if (out.error) throw Object.assign(new Error(out.error.message), { code: out.error.code });
      Object.assign(row, {
        grams: +(out.ok.partGrams + out.ok.supportGrams).toFixed(3),
        partGrams: +out.ok.partGrams.toFixed(3),
        supportGrams: +out.ok.supportGrams.toFixed(3),
        size: out.ok.size.map((x) => +x.toFixed(1)),
        fitsBed: out.ok.fitsBed,
        warnings: out.ok.warnings,
        measureMs: Math.round(performance.now() - t),
        stagesMs: Object.fromEntries(Object.entries(stages).map(([k, v]) => [k, Math.round(v)])),
      });
    }
    if (mode === 'printTime' || mode === 'both') {
      const t = performance.now();
      const { out } = call(engine.printTime, input);
      if (out.error) throw Object.assign(new Error(out.error.message), { code: out.error.code });
      const density = +set.filament.filament_density[0];
      Object.assign(row, {
        seconds: Math.round(out.ok.seconds),
        gcodePartGrams: +((out.ok.modelMm3 * density) / 1000).toFixed(3),
        gcodeSupportGrams: +((out.ok.supportMm3 * density) / 1000).toFixed(3),
        gcodeCustomGrams: +((out.ok.customMm3 * density) / 1000).toFixed(3),
        printTimeMs: Math.round(performance.now() - t),
      });
    }
  } catch (err) {
    row.error = err.code ? `${err.code}: ${err.message}` : String(err.message || err);
  }
  console.log(JSON.stringify(row));
}
