// Экспорт моста в JavaScript (embind). Ответ — JSON-строка: так не нужно
// описывать структуры в embind, а ошибки не пересекают границу C++/JS исключением.
//
//   measure({data: Uint8Array, fileName, scale, machine, process, filament}, onProgress) → string
//   printTime({…те же поля}, onProgress) → string
//   fullConfig(machine, process, filament) → string («ключ = значение» по строке)
//   preparedStl({…}) → Uint8Array: модель после автоповорота и расстановки, для сверки
//
// onProgress(stage: string, percent: number) вызывается синхронно из нарезки.
// Ответ: {"ok": {...}} или {"error": {"code": "...", "message": "..."}}.
#ifdef __EMSCRIPTEN__

#include "bridge.hpp"

#include <libslic3r/Utils.hpp>

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <nlohmann/json.hpp>

#include <new>

using emscripten::val;
using nlohmann::json;

namespace {

pf::JobInput read_input(const val &input)
{
    pf::JobInput in;
    in.file_name     = input["fileName"].as<std::string>();
    in.data          = emscripten::convertJSArrayToNumberVector<std::uint8_t>(input["data"]);
    in.scale         = input["scale"].isNumber() ? input["scale"].as<double>() : 1.;
    in.machine_json  = input["machine"].as<std::string>();
    in.process_json  = input["process"].as<std::string>();
    in.filament_json = input["filament"].as<std::string>();
    return in;
}

pf::ProgressFn progress_of(const val &fn)
{
    if (fn.isUndefined() || fn.isNull())
        return {};
    return [fn](pf::Stage stage, int percent) { fn(std::string(pf::stage_name(stage)), percent); };
}

json size_json(const double (&size)[3]) { return json::array({size[0], size[1], size[2]}); }

std::string error_json(const char *code, const std::string &message)
{
    return json{{"error", {{"code", code}, {"message", message}}}}.dump();
}

template<class F> std::string run(F &&f)
{
    try {
        return json{{"ok", f()}}.dump();
    } catch (const pf::BridgeError &ex) {
        return error_json(pf::error_code_name(ex.code), ex.what());
    } catch (const std::bad_alloc &) {
        return error_json("out_of_memory", "out of memory");
    } catch (const std::exception &ex) {
        return error_json("slice_failed", ex.what());
    } catch (...) {
        return error_json("slice_failed", "unknown error");
    }
}

std::string js_measure(val input, val on_progress)
{
    return run([&] {
        const pf::MeasureResult r = pf::measure(read_input(input), progress_of(on_progress));
        return json{{"partMm3", r.part_mm3},
                    {"supportMm3", r.support_mm3},
                    {"density", r.density},
                    {"partGrams", r.part_grams()},
                    {"supportGrams", r.support_grams()},
                    {"size", size_json(r.size)},
                    {"fitsBed", r.fits_bed},
                    {"warnings", r.warnings.codes()}};
    });
}

std::string js_print_time(val input, val on_progress)
{
    return run([&] {
        const pf::PrintTimeResult r = pf::print_time(read_input(input), progress_of(on_progress));
        return json{{"seconds", r.seconds},
                    {"modelMm3", r.model_mm3},
                    {"supportMm3", r.support_mm3},
                    {"customMm3", r.custom_mm3},
                    {"size", size_json(r.size)},
                    {"fitsBed", r.fits_bed},
                    {"warnings", r.warnings.codes()},
                    {"statistics", json::parse(r.statistics_json, nullptr, false)}};
    });
}

// Двоичный STL подготовленной модели (Uint8Array) или строка «error: …».
val js_prepared_stl(val input)
{
    try {
        const pf::PreparedStl r = pf::prepared_stl(read_input(input));
        val view = val(emscripten::typed_memory_view(r.bytes.size(), r.bytes.data()));
        return val::global("Uint8Array").new_(view); // копия: r.bytes уйдёт со стека
    } catch (const std::exception &ex) {
        return val(std::string("error: ") + ex.what());
    }
}

std::string js_full_config(std::string machine, std::string process, std::string filament)
{
    try {
        return pf::full_config_dump(machine, process, filament);
    } catch (const std::exception &ex) {
        return std::string("error: ") + ex.what();
    }
}

} // namespace

EMSCRIPTEN_BINDINGS(protoform_bridge)
{
    // Журнал Orca только об ошибках: подробный журнал заметно замедляет нарезку.
    Slic3r::set_logging_level(1);
    Slic3r::set_temporary_dir("/tmp");
    emscripten::function("measure", &js_measure);
    emscripten::function("printTime", &js_print_time);
    emscripten::function("fullConfig", &js_full_config);
    emscripten::function("preparedStl", &js_prepared_stl);
}

#endif // __EMSCRIPTEN__
