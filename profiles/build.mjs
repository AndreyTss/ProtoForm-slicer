// Собирает profiles/a1.json из системных профилей OrcaSlicer (orca/resources/profiles).
//
// Разворачивает наследование так же, как PresetBundle::resolve_vendor_preset:
// ключи родителя (inherits), поверх них ключи шаблонов из include по порядку,
// поверх всё, что задано в самом профиле. Ищет профиль сначала в наборе BBL,
// потом в OrcaFilamentLibrary (базовый набор пластиков). Ключей, которых нет
// ни в одном звене цепочки, в итоге нет: их значения движок берёт из
// FullPrintConfig, как и OrcaSlicer в режиме командной строки.
//
// Ограничения cli_safe_* из BBL/cli_config.json не применяются: в установленном
// OrcaSlicer этого файла нет, и ни окно программы, ни командная строка их не ставят.
//
// node profiles/build.mjs [папка resources/profiles] [выходной файл]
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(process.argv[2] || path.join(here, '../orca/resources/profiles'));
const out = path.resolve(process.argv[3] || path.join(here, 'a1.json'));

const MACHINE = 'Bambu Lab A1 0.4 nozzle';
const PROCESS = {
  '0.08': '0.08mm Extra Fine @BBL A1',
  '0.12': '0.12mm Fine @BBL A1',
  '0.16': '0.16mm Optimal @BBL A1',
  '0.2': '0.20mm Standard @BBL A1',
  '0.24': '0.24mm Draft @BBL A1',
  '0.28': '0.28mm Extra Draft @BBL A1',
};
// pa6gf: профиля PA-GF для A1 в наборе нет, берём ближайший наполненный нейлон.
const FILAMENT = {
  pla: 'Generic PLA @BBL A1',
  petg: 'Generic PETG @BBL A1',
  abs: 'Generic ABS @BBL A1',
  asa: 'Generic ASA @BBL A1',
  flex: 'Generic TPU @BBL A1',
  pa6gf: 'Generic PA-CF @BBL A1',
};
// Поверх профиля толщины: поддержки включены всегда, «прочная деталь» меняет два ключа.
const SUPPORT = { enable_support: '1' };
const STRONG = { sparse_infill_density: '40%', wall_loops: '4' };

// Служебные ключи файла, которые не становятся настройками (Preset.hpp, BBL_JSON_KEY_*).
const META = new Set(['inherits', 'include', 'instantiation', 'description', 'renamed_from', 'alias', 'version', 'url']);

function vendorIndex(vendor) {
  const index = JSON.parse(readFileSync(path.join(root, `${vendor}.json`), 'utf8'));
  const map = new Map();
  for (const kind of ['machine_list', 'process_list', 'filament_list', 'machine_model_list'])
    for (const e of index[kind] || []) map.set(`${kind.split('_')[0]}:${e.name}`, path.join(root, vendor, e.sub_path));
  return { version: index.version, map };
}
const vendors = [vendorIndex('BBL'), vendorIndex('OrcaFilamentLibrary')];

function readPreset(type, name) {
  for (const v of vendors) {
    const file = v.map.get(`${type}:${name}`);
    if (file) return JSON.parse(readFileSync(file, 'utf8'));
  }
  throw new Error(`нет профиля ${type} "${name}"`);
}

/** Собственные ключи профиля со всей цепочкой inherits и include, без служебных. */
function resolve(type, name, seen = new Set()) {
  if (seen.has(name)) throw new Error(`цикл наследования на "${name}"`);
  seen.add(name);
  const own = readPreset(type, name);
  let cfg = own.inherits ? resolve(type, own.inherits, seen) : {};
  cfg = { ...cfg };
  const includes = own.include == null ? [] : Array.isArray(own.include) ? own.include : [own.include];
  for (const inc of includes) Object.assign(cfg, strip(resolve(type, inc, new Set(seen))));
  Object.assign(cfg, strip(own));
  return cfg;
}
function strip(cfg) {
  const o = {};
  for (const [k, v] of Object.entries(cfg)) if (!META.has(k)) o[k] = v;
  return o;
}

/** Развёрнутый системный профиль в виде, который читает ConfigBase::load_from_json. */
function flatten(type, name) {
  const cfg = resolve(type, name);
  // Имя, тип и источник — от самого профиля, а не от родителя.
  return { ...cfg, type, name, from: 'system', inherits: '' };
}

const machine = flatten('machine', MACHINE);

// Тип стола. Окно Orca берёт его из модели принтера (default_bed_type), командная
// строка — из FullPrintConfig (Cool Plate), а на Cool Plate PETG, ABS, ASA и PA
// не проходят Print::validate. Ставим стол как в окне — поверх процесса, чтобы
// OrcaSlicer на компьютере получил то же самое.
const machineModel = JSON.parse(readFileSync(vendors[0].map.get(`machine:${machine.printer_model}`), 'utf8'));
const PLATE = { curr_bed_type: machineModel.default_bed_type };
const process_ = Object.fromEntries(Object.entries(PROCESS).map(([k, n]) => [k, flatten('process', n)]));
const filament = Object.fromEntries(Object.entries(FILAMENT).map(([k, n]) => [k, flatten('filament', n)]));

// pa6gf — SUNLU PA6-GF (25% стекловолокна), а ближайший профиль с углеволокном:
// плотность из паспорта производителя, иначе масса выходит на четверть меньше.
filament.pa6gf.filament_density = ['1.269'];

// Проверки того, на что опирается мост: один экструдер и один вариант пластика.
if (machine.nozzle_diameter.length !== 1) throw new Error('ожидался один экструдер');
for (const [k, f] of Object.entries(filament)) {
  if ((f.filament_extruder_variant || ['x']).length !== 1) throw new Error(`${k}: несколько вариантов экструдера`);
  if (!f.filament_density) throw new Error(`${k}: нет плотности`);
}
for (const [k, p] of Object.entries(process_))
  if (Math.abs(+p.layer_height - +k) > 1e-9) throw new Error(`${k}: layer_height ${p.layer_height}`);

const sha = (o) => createHash('sha256').update(JSON.stringify(o)).digest('hex').slice(0, 16);
const result = {
  source: { orcaVendorVersion: vendors[0].version, libraryVersion: vendors[1].version },
  overrides: { support: SUPPORT, plate: PLATE, strong: STRONG },
  machine,
  process: process_,
  filament,
  hashes: {
    machine: sha(machine),
    process: Object.fromEntries(Object.entries(process_).map(([k, v]) => [k, sha(v)])),
    filament: Object.fromEntries(Object.entries(filament).map(([k, v]) => [k, sha(v)])),
  },
};
writeFileSync(out, JSON.stringify(result, null, 1) + '\n');
console.log(`${out}: ${(JSON.stringify(result).length / 1024).toFixed(0)} КБ`);
for (const [k, f] of Object.entries(filament))
  console.log(`  ${k.padEnd(6)} ${f.name.padEnd(26)} density ${f.filament_density} flow ${f.filament_flow_ratio} type ${f.filament_type}`);
