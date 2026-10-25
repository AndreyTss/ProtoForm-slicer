// Нативная проверка моста без браузера (сборка с -DPF_BRIDGE_CLI, не для WebAssembly):
//   pf-bridge measure|time <model> <machine.json> <process.json> <filament.json> [scale]
//   pf-bridge config <machine.json> <process.json> <filament.json>
//   pf-bridge prepare <model> <machine.json> <process.json> <filament.json> <out.stl> [scale]
// Печатает тот же JSON, что и bindings.cpp, ход работы — в stderr.
#if !defined(__EMSCRIPTEN__) && defined(PF_BRIDGE_CLI)

#include "bridge.hpp"

#include <libslic3r/Utils.hpp>

#include <nlohmann/json.hpp>

#include <boost/nowide/fstream.hpp>

#include <cstdio>
#include <iostream>
#include <iterator>
#include <new>

using nlohmann::json;

namespace {

std::string read_file(const char *path)
{
    boost::nowide::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error(std::string("cannot read ") + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

json size_json(const double (&size)[3]) { return json::array({size[0], size[1], size[2]}); }

} // namespace

int main(int argc, char **argv)
{
    Slic3r::set_logging_level(1);
    const std::string mode = argc > 1 ? argv[1] : "";
    try {
        if (mode == "config" && argc == 5) {
            std::cout << pf::full_config_dump(read_file(argv[2]), read_file(argv[3]), read_file(argv[4]));
            return 0;
        }
        if (mode == "prepare" && argc >= 7) {
            pf::JobInput in;
            in.file_name = argv[2];
            const std::string data = read_file(argv[2]);
            in.data.assign(data.begin(), data.end());
            in.machine_json  = read_file(argv[3]);
            in.process_json  = read_file(argv[4]);
            in.filament_json = read_file(argv[5]);
            in.scale         = argc > 7 ? std::atof(argv[7]) : 1.;
            const pf::PreparedStl r = pf::prepared_stl(in);
            boost::nowide::ofstream out(argv[6], std::ios::binary);
            out.write(reinterpret_cast<const char *>(r.bytes.data()), std::streamsize(r.bytes.size()));
            std::cout << json{{"ok", {{"size", size_json(r.size)}, {"fitsBed", r.fits_bed}, {"repaired", r.repaired}}}}.dump() << "\n";
            return 0;
        }
        if ((mode != "measure" && mode != "time") || argc < 6) {
            std::cerr << "usage: pf-bridge measure|time <model> <machine> <process> <filament> [scale]\n"
                         "       pf-bridge prepare <model> <machine> <process> <filament> <out.stl> [scale]\n"
                         "       pf-bridge config <machine> <process> <filament>\n";
            return 2;
        }
        pf::JobInput in;
        in.file_name     = argv[2];
        const std::string data = read_file(argv[2]);
        in.data.assign(data.begin(), data.end());
        in.machine_json  = read_file(argv[3]);
        in.process_json  = read_file(argv[4]);
        in.filament_json = read_file(argv[5]);
        in.scale         = argc > 6 ? std::atof(argv[6]) : 1.;
        const pf::ProgressFn progress = [](pf::Stage s, int p) { std::fprintf(stderr, "%s %d\n", pf::stage_name(s), p); };

        json out;
        if (mode == "measure") {
            const pf::MeasureResult r = pf::measure(in, progress);
            out = {{"partMm3", r.part_mm3}, {"supportMm3", r.support_mm3}, {"density", r.density},
                   {"partGrams", r.part_grams()}, {"supportGrams", r.support_grams()}, {"size", size_json(r.size)},
                   {"fitsBed", r.fits_bed}, {"warnings", r.warnings.codes()}};
        } else {
            const pf::PrintTimeResult r = pf::print_time(in, progress);
            out = {{"seconds", r.seconds}, {"modelMm3", r.model_mm3}, {"supportMm3", r.support_mm3},
                   {"customMm3", r.custom_mm3}, {"size", size_json(r.size)}, {"fitsBed", r.fits_bed},
                   {"warnings", r.warnings.codes()}, {"statistics", json::parse(r.statistics_json, nullptr, false)}};
        }
        std::cout << json{{"ok", out}}.dump() << "\n";
        return 0;
    } catch (const pf::BridgeError &ex) {
        std::cout << json{{"error", {{"code", pf::error_code_name(ex.code)}, {"message", ex.what()}}}}.dump() << "\n";
    } catch (const std::bad_alloc &) {
        std::cout << json{{"error", {{"code", "out_of_memory"}, {"message", "out of memory"}}}}.dump() << "\n";
    } catch (const std::exception &ex) {
        std::cout << json{{"error", {{"code", "slice_failed"}, {"message", ex.what()}}}}.dump() << "\n";
    }
    return 1;
}

#endif
