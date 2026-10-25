// Эталон: нарезка тех же моделей OrcaSlicer на компьютере, командной строкой, без окна.
//
// node orca-ref.mjs --orca <путь к orca-slicer> --out <папка> [--layer 0.2] [--material pla]
//                   [--strong] [--arrange 0] <модель>...
//
// Профили — те же, что у движка (profiles/a1.json через js/profiles.js).
// Модели лучше давать подготовленные движком (engine-run.mjs --prepared): они уже
// повёрнуты и стоят на столе, а --arrange 0 оставляет их на месте.
// Граммы считаются по ролям из G-code (gcode-roles.mjs) — без роли Custom, то есть
// без линии прочистки сопла, как у движка. Время — из шапки G-code.
import { readFileSync, existsSync, rmSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { pickProfiles } from '../js/profiles.js';
import { writePresets } from './presets.mjs';
import { summarize } from './gcode-roles.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const flag = (k) => { const i = args.indexOf(k); if (i < 0) return false; args.splice(i, 1); return true; };

const orca = opt('--orca', null);
const outArg = opt('--out', null);
const outDir = outArg && path.resolve(outArg);
const layer = opt('--layer', '0.2');
const material = opt('--material', 'pla');
const strong = flag('--strong');
const arrange = opt('--arrange', '0');
const timeoutS = Number(opt('--timeout', '600'));
const profilesPath = opt('--profiles', path.join(here, '../profiles/a1.json'));
const models = args.map((m) => path.resolve(m)); // Orca запускается из папки результата
if (!orca || !outDir || models.length === 0) {
  console.error('usage: node orca-ref.mjs --orca <orca-slicer> --out <dir> [--layer 0.2] [--material pla] [--strong] [--arrange 0] <model>...');
  process.exit(2);
}

const profiles = JSON.parse(readFileSync(profilesPath, 'utf8'));
const set = pickProfiles(profiles, { layer, material, strong });
const presets = writePresets(path.join(outDir, `presets-${layer}-${material}${strong ? '-strong' : ''}`), set);
const density = +set.filament.filament_density[0];
const diameter = +set.filament.filament_diameter[0];

/** «1h 2m 3s» → секунды. */
function parseDuration(text) {
  let s = 0;
  for (const [, n, u] of (text || '').matchAll(/(\d+)\s*([dhms])/g)) s += +n * { d: 86400, h: 3600, m: 60, s: 1 }[u];
  return s;
}

for (const model of models) {
  const name = path.basename(model).replace(/\.[^.]+$/, '');
  const dir = path.join(outDir, `${name}-${layer}-${material}${strong ? '-strong' : ''}`);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const t = performance.now();
  const run = spawnSync(orca, [
    '--slice', '0',
    '--arrange', arrange,
    '--export-statistics',
    '--outputdir', dir,
    '--load-settings', `${presets.machine};${presets.process}`,
    '--load-filaments', presets.filament,
    model,
  ], { encoding: 'utf8', timeout: timeoutS * 1000, maxBuffer: 64 << 20, cwd: dir }); // журнал 00000.log Orca пишет в текущую папку
  const row = { file: path.basename(model), layer, material, strong, s: +((performance.now() - t) / 1000).toFixed(1) };
  const gcode = path.join(dir, 'plate_1.gcode');
  if (!existsSync(gcode)) {
    row.error = `exit ${run.status}${run.error ? ' ' + run.error.message : ''}: ${(run.stderr || run.stdout || '').trim().split('\n').slice(-2).join(' | ')}`;
    console.log(JSON.stringify(row));
    continue;
  }
  const text = readFileSync(gcode, 'utf8');
  const sum = summarize(text, density, diameter);
  const time = /total estimated time: ([^;\n]+)/.exec(text)?.[1]?.trim();
  Object.assign(row, {
    grams: sum.grams, partGrams: sum.partGrams, supportGrams: sum.supportGrams, customGrams: sum.customGrams,
    headerGrams: sum.headerG, time, seconds: parseDuration(time), roles: sum.roles,
  });
  const statsFile = path.join(dir, 'plate_1.statistics.json');
  if (existsSync(statsFile)) {
    try {
      const stats = JSON.parse(readFileSync(statsFile, 'utf8'));
      row.statsSeconds = stats.time ?? stats.summary?.time;
    } catch { /* статистика необязательна */ }
  }
  console.log(JSON.stringify(row));
}
