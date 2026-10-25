// Слияние профилей машины, процесса и одного пластика в конфигурацию печати.
// Повторяет CLI::run (src/OrcaSlicer.cpp, OrcaSlicer v2.5.0-alpha) для вызова
//   --load-settings "machine.json;process.json" --load-filaments "filament.json"
// с развёрнутыми системными профилями. Ветки CLI для 3MF-проектов, смены
// принтера, нескольких пластиков и ключей командной строки здесь не нужны
// и опущены. Порядок шагов сохранён, чтобы сверять с новыми версиями Orca.
#include "internal.hpp"

#include <libslic3r/Config.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <map>
#include <memory>
#include <set>

using namespace Slic3r;

namespace pf {
namespace {

[[noreturn]] void profile_error(const std::string &message)
{
    // Профили собирает наш скрипт, ошибка в них — не вина входного файла.
    throw BridgeError(ErrorCode::SliceFailed, "profile: " + message);
}

struct Loaded {
    DynamicPrintConfig config;
    std::string        name, from, type, filament_id;
};

// load_config_file в CLI: load_from_json, проверка from и type, normalize_fdm.
Loaded load_preset(const WorkDir &dir, const std::string &file, const std::string &json, const std::string &expected_type)
{
    dir.write(file, json.data(), json.size());
    Loaded                             out;
    std::map<std::string, std::string> key_values;
    std::string                        reason;
    out.config.load_from_json(dir.file(file), ForwardCompatibilitySubstitutionRule::Enable, key_values, reason);
    if (!reason.empty())
        profile_error(expected_type + ": " + reason);
    out.name = key_values[BBL_JSON_KEY_NAME];
    out.from = key_values[BBL_JSON_KEY_FROM];
    out.type = key_values[BBL_JSON_KEY_TYPE];
    if (out.from != "system")
        profile_error(expected_type + " \"" + out.name + "\" is not a system preset");
    if (out.type != expected_type)
        profile_error("expected " + expected_type + ", got \"" + out.type + "\"");
    if (const auto *inherits = out.config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS); inherits && !inherits->value.empty())
        profile_error(expected_type + " \"" + out.name + "\" is not flattened");
    if (auto it = key_values.find(BBL_JSON_KEY_FILAMENT_ID); it != key_values.end())
        out.filament_id = it->second;
    out.config.normalize_fdm();
    return out;
}

enum class PresetKind { Printer, Print, Filament };

// load_default_gcodes_to_config (OrcaSlicer.cpp): G-code, которого нет в профиле,
// становится пустым, а не берётся из FullPrintConfig.
void load_default_gcodes(DynamicPrintConfig &config, PresetKind kind)
{
    if (config.size() == 0)
        return;
    if (kind == PresetKind::Printer) {
        for (const char *key : {"change_filament_gcode", "layer_change_gcode", "machine_end_gcode", "machine_pause_gcode",
                                "machine_start_gcode", "template_custom_gcode", "printing_by_object_gcode",
                                "before_layer_change_gcode", "time_lapse_gcode", "wrapping_detection_gcode"})
            config.option<ConfigOptionString>(key, true);
    } else if (kind == PresetKind::Filament) {
        for (const char *key : {"filament_start_gcode", "filament_end_gcode"}) {
            std::vector<std::string> &values = config.option<ConfigOptionStrings>(key, true)->values;
            if (values.empty())
                values.resize(1, std::string());
        }
    }
}

// update_full_config(…, update_all = true): все ключи, кроме служебных.
void update_full_config(DynamicPrintConfig &full, const DynamicPrintConfig &config)
{
    static const std::set<std::string> skipped{"compatible_prints", "compatible_printers", "model_id", "inherits",
                                               "dev_model_name", "name", "from", "type", "version", "setting_id",
                                               "instantiation"};
    for (const t_config_option_key &key : config.keys()) {
        if (skipped.count(key))
            continue;
        const ConfigOption *src = config.option(key);
        ConfigOption       *dst = full.option(key, true);
        if (src == nullptr || dst == nullptr)
            profile_error("cannot merge \"" + key + "\"");
        dst->set(src);
    }
}

void set_string_at(DynamicPrintConfig &config, const char *key, const std::string &value, size_t index, size_t count)
{
    auto              *opt = static_cast<ConfigOptionStrings *>(config.option(key, true));
    ConfigOptionString item(value);
    if (opt->size() < count)
        opt->resize(count, &item);
    opt->set_at(&item, index, 0);
}

PrinterTechnology technology_of(const DynamicPrintConfig &config)
{
    const auto *opt = config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    return opt == nullptr ? ptUnknown : opt->value;
}

// Ключи, которые CLI добавляет к своим параметрам всегда (OrcaSlicer.cpp, «Filament Track Switch»).
DynamicPrintConfig extra_config()
{
    DynamicPrintConfig extra;
    extra.set_key_value("has_filament_switcher", new ConfigOptionBool(false));
    extra.set_key_value("enable_filament_dynamic_map", new ConfigOptionBool(false));
    extra.normalize_fdm();
    return extra;
}

// Пластик в ячейку 1 (filament_index = 1, old_start = 0, один вариант в ячейке).
void merge_filament(DynamicPrintConfig &m, Loaded &fil)
{
    const int filament_count = 1, filament_index = 1, old_start = 0, old_variant_count = 1;

    // В конфиге машины и процесса filament_self_index нет — CLI создаёт его заново.
    m.option<ConfigOptionInts>("filament_self_index", true)->values = {1};
    std::vector<int> new_variant_counts{1};

    for (const std::string &key : filament_options_with_variant)
        if (key != "filament_extruder_variant" && fil.config.has(key))
            m.option(key, true);
    normalize_filament_values_to_variants(m);

    load_default_gcodes(fil.config, PresetKind::Filament);

    set_string_at(m, "filament_settings_id", fil.name, filament_index - 1, filament_count);
    fil.config.erase("filament_settings_id");
    set_string_at(m, "filament_ids", fil.filament_id, filament_index - 1, filament_count);

    auto *curr_variant = m.option<ConfigOptionStrings>("filament_extruder_variant");
    if (curr_variant == nullptr) {
        curr_variant = m.option<ConfigOptionStrings>("filament_extruder_variant", true);
        curr_variant->values.resize(filament_count, get_extruder_variant_string(etDirectDrive, nvtStandard));
    }
    if (int(curr_variant->values.size()) < filament_count)
        curr_variant->values.resize(filament_count, get_extruder_variant_string(etDirectDrive, nvtStandard));
    const auto *new_variant = dynamic_cast<const ConfigOptionStrings *>(fil.config.option("filament_extruder_variant", true));
    const int   new_variant_count = int(new_variant->size());

    std::vector<int> new_variant_indice(new_variant_count, -1);
    for (int i = 0; i < new_variant_count; ++i)
        for (int j = old_start; j < old_start + old_variant_count && j < int(curr_variant->values.size()); ++j)
            if (curr_variant->values[j] == new_variant->values[i]) {
                new_variant_indice[i] = j;
                break;
            }

    static const std::set<std::string> skipped_vectors{"compatible_prints", "compatible_printers", "model_id", "dev_model_name",
                                                       "filament_settings_id"};
    for (const t_config_option_key &key : fil.config.keys()) {
        const ConfigOption *src = fil.config.option(key);
        if (src->is_scalar()) {
            if (key == "compatible_printers_condition") {
                auto *dst = static_cast<ConfigOptionStrings *>(m.option("compatible_machine_expression_group", true));
                if (dst->size() == 0) {
                    ConfigOptionString empty;
                    dst->resize(filament_count + 2, &empty);
                }
                dst->set_at(src, filament_index, 0);
            } else if (key == "compatible_prints_condition") {
                auto *dst = static_cast<ConfigOptionStrings *>(m.option("compatible_process_expression_group", true));
                if (dst->size() == 0) {
                    ConfigOptionString empty;
                    dst->resize(filament_count, &empty);
                }
                dst->set_at(src, filament_index - 1, 0);
            }
            continue;
        }
        if (skipped_vectors.count(key) || filament_dev_options.count(key))
            continue;
        ConfigOption *opt = m.option(key, true);
        if (opt == nullptr)
            profile_error("cannot create \"" + key + "\"");
        auto       *dst     = static_cast<ConfigOptionVectorBase *>(opt);
        const auto *src_vec = static_cast<const ConfigOptionVectorBase *>(src);
        if (filament_options_with_variant.count(key)) {
            std::vector<int> temp_variant_indice(new_variant_count, -1);
            if (src_vec->size() > 0 && dst->size() < size_t(old_start + old_variant_count))
                dst->resize(size_t(old_start + old_variant_count), src_vec);
            dst->set_with_restore_2(src_vec, temp_variant_indice, old_start, old_variant_count, true);
            if (key == "filament_extruder_variant")
                new_variant_counts[filament_index - 1] = int(src_vec->size());
        } else {
            dst->set_at(src_vec, filament_index - 1, 0);
        }
    }

    // Варианты, которых в пластике нет, сохраняют прежние значения.
    std::vector<int> kept_variant_indice = new_variant_indice;
    for (int &i : kept_variant_indice)
        if (i < 0)
            i = old_start;
    for (const std::string &key : filament_options_with_variant) {
        if (fil.config.has(key))
            continue;
        auto *dst = dynamic_cast<ConfigOptionVectorBase *>(m.option(key));
        if (dst == nullptr || dst->size() < size_t(old_start + old_variant_count))
            continue;
        std::unique_ptr<ConfigOption> old_values(dst->clone());
        dst->set_with_restore_2(static_cast<ConfigOptionVectorBase *>(old_values.get()), kept_variant_indice, old_start, old_variant_count, true);
    }

    set_filament_dev_options(m, {&fil.config});

    if (m.option<ConfigOptionStrings>("filament_extruder_variant")) {
        std::vector<int> &self_indice = m.option<ConfigOptionInts>("filament_self_index", true)->values;
        self_indice.resize(m.option<ConfigOptionStrings>("filament_extruder_variant")->size(), 1);
        int k = 0;
        for (int i = 0; i < filament_count; ++i)
            for (int j = 0; j < new_variant_counts[i]; ++j)
                self_indice[k++] = i + 1;
    }
}

// Матрица промывки (OrcaSlicer.cpp после пластиков): при одном пластике промывать нечего,
// все значения нули. Считается, только если в профилях её нет, как и в CLI.
void flush_matrix(DynamicPrintConfig &m, int extruder_count)
{
    auto *colours = m.option<ConfigOptionStrings>("filament_colour");
    if (colours == nullptr || m.option<ConfigOptionFloats>("flush_volumes_matrix") != nullptr)
        return;
    const size_t count = colours->values.size();
    if (count == 0)
        return;
    if (count != 1)
        profile_error("one filament expected");
    if (m.option<ConfigOptionBools>("filament_is_support", true)->size() != count)
        profile_error("filament_is_support does not match filament_colour");
    m.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values.assign(count * count * size_t(extruder_count), 0.);
    m.option<ConfigOptionFloats>("flush_multiplier", true)->values.resize(size_t(extruder_count), 1.);
}

} // namespace

MergedConfig merge_profiles(const WorkDir &dir, const std::string &machine_json, const std::string &process_json,
                            const std::string &filament_json)
{
    MergedConfig        out;
    DynamicPrintConfig &m = out.config;

    Loaded machine  = load_preset(dir, "machine.json", machine_json, "machine");
    Loaded process  = load_preset(dir, "process.json", process_json, "process");
    Loaded filament = load_preset(dir, "filament.json", filament_json, "filament");

    PrinterTechnology tech = ptUnknown;
    for (const Loaded *p : {&machine, &process, &filament}) {
        const PrinterTechnology other = technology_of(p->config);
        if (tech == ptUnknown)
            tech = other;
        if (other != ptUnknown && other != tech)
            profile_error("mixed printer technologies");
    }

    machine.config.set("printer_settings_id", machine.name, true);
    process.config.set("print_settings_id", process.name, true);
    std::vector<std::string> print_compatible_printers = process.config.option<ConfigOptionStrings>("compatible_printers", true)->values;
    std::string              different_process_setting;
    if (const auto *diff = process.config.option<ConfigOptionStrings>("different_settings_to_system"); diff && !diff->values.empty())
        different_process_setting = diff->values[0];

    const int filament_count = 1;
    std::vector<std::string> &different_settings = m.option<ConfigOptionStrings>("different_settings_to_system", true)->values;
    std::vector<std::string> &inherits_group     = m.option<ConfigOptionStrings>("inherits_group", true)->values;
    inherits_group.resize(filament_count + 2, std::string());
    different_settings.resize(filament_count + 2, std::string());
    if (!different_process_setting.empty())
        different_settings[0] = different_process_setting;

    // Машина: все ключи. Системный профиль — inherits_group и different_settings пустые.
    load_default_gcodes(machine.config, PresetKind::Printer);
    update_full_config(m, machine.config);
    if (const auto *nozzles = m.option<ConfigOptionFloats>("nozzle_diameter"))
        out.extruder_count = int(nozzles->values.size());
    m.option<ConfigOptionStrings>("printer_extruder_variant", true);

    // Процесс.
    m.option<ConfigOptionStrings>("print_compatible_printers", true)->values = std::move(print_compatible_printers);
    load_default_gcodes(process.config, PresetKind::Print);
    update_full_config(m, process.config);
    m.option<ConfigOptionStrings>("print_extruder_variant", true);

    merge_filament(m, filament);
    flush_matrix(m, out.extruder_count);

    if (tech == ptUnknown)
        tech = ptFFF;
    if (tech != ptFFF)
        profile_error("only FFF printers are supported");

    m.apply(extra_config(), true);
    m.normalize_fdm();
    resize_mixed_filament_metadata(m, size_t(filament_count), size_t(filament_count));
    m.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology", true)->value = tech;

    {
        FullPrintConfig fff;
        fff.apply(m, true);
        m.apply(fff, true);
        normalize_filament_values_to_variants(m);
    }

    const std::map<std::string, std::string> invalid = m.validate(true);
    if (!invalid.empty()) {
        std::string message = "invalid values:";
        for (const auto &[key, reason] : invalid)
            message += " " + key + ": " + reason + ";";
        profile_error(message);
    }
    m.option<ConfigOptionBool>("enable_wrapping_detection", true);
    m.opt<ConfigOptionPoints>("wrapping_exclude_area", true);

    out.printer_name   = machine.name;
    out.filament_count = filament_count;
    return out;
}

DynamicPrintConfig plate_config(const MergedConfig &merged)
{
    DynamicPrintConfig cfg = merged.config;
    // Конфиг первой пластины PartPlate по умолчанию.
    DynamicPrintConfig plate;
    plate.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = fmmAutoForFlush;
    cfg.apply(plate);
    cfg.apply(extra_config(), true);

    std::vector<int> &filament_map = cfg.option<ConfigOptionInts>("filament_map", true)->values;
    if (int(filament_map.size()) < merged.filament_count)
        filament_map.resize(size_t(merged.filament_count), 1);
    if (merged.extruder_count == 1)
        for (int i = 0; i < merged.filament_count; ++i)
            filament_map[size_t(i)] = 1;
    if (!cfg.has("nozzle_volume_type"))
        cfg.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values.resize(size_t(merged.extruder_count), nvtStandard);
    return cfg;
}

std::string full_config_dump(const std::string &machine_json, const std::string &process_json, const std::string &filament_json)
{
    WorkDir            dir;
    MergedConfig       merged = merge_profiles(dir, machine_json, process_json, filament_json);
    DynamicPrintConfig cfg    = plate_config(merged);
    std::string        out;
    for (const std::string &key : cfg.keys())
        out += key + " = " + cfg.opt_serialize(key) + "\n";
    return out;
}

} // namespace pf
