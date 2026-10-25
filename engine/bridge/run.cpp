// Работы моста: measure (объём по траекториям) и print_time (G-code и GCodeProcessor).
// Порядок вызовов libslic3r — как в CLI::run (src/OrcaSlicer.cpp) для одной пластины.
#include "internal.hpp"

#include <libslic3r/BuildVolume.hpp>
#include <libslic3r/Exception.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/GCode/GCodeProcessor.hpp>
#include <libslic3r/GCode/GCodeStatistics.hpp>
#include <libslic3r/Layer.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/PrintBase.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <iterator>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <new>

using namespace Slic3r;

namespace pf {

const char *error_code_name(ErrorCode code)
{
    switch (code) {
    case ErrorCode::BadFile: return "bad_file";
    case ErrorCode::EmptyModel: return "empty_model";
    case ErrorCode::SliceFailed: return "slice_failed";
    case ErrorCode::OutOfMemory: return "out_of_memory";
    }
    return "slice_failed";
}

const char *stage_name(Stage stage)
{
    switch (stage) {
    case Stage::Read: return "read";
    case Stage::Repair: return "repair";
    case Stage::Orient: return "orient";
    case Stage::Slice: return "slice";
    case Stage::Support: return "support";
    case Stage::Measure: return "measure";
    case Stage::GCode: return "gcode";
    }
    return "slice";
}

std::vector<std::string> Warnings::codes() const
{
    std::vector<std::string> out;
    if (repaired)
        out.emplace_back("repaired");
    if (thin_walls)
        out.emplace_back("thin_walls");
    if (floating_parts)
        out.emplace_back("floating_parts");
    return out;
}

WorkDir::WorkDir()
{
    static std::atomic<unsigned> counter{0};
    m_path = "/tmp/pf-job-" + std::to_string(++counter);
    boost::system::error_code ec;
    boost::filesystem::remove_all(m_path, ec);
    boost::filesystem::create_directories(m_path, ec);
    if (ec)
        throw BridgeError(ErrorCode::SliceFailed, "cannot create " + m_path + ": " + ec.message());
}

WorkDir::~WorkDir()
{
    boost::system::error_code ec;
    boost::filesystem::remove_all(m_path, ec);
}

void WorkDir::write(const std::string &name, const void *data, size_t size) const
{
    boost::nowide::ofstream out(file(name), std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char *>(data), std::streamsize(size));
    out.close();
    if (!out)
        throw BridgeError(ErrorCode::SliceFailed, "cannot write " + file(name));
}

namespace {

// Доли общей шкалы 0–100 по этапам. Нарезка Orca сообщает 0–80 %, G-code — по слоям.
struct Scale {
    int read, repair, orient, slice, after_slice, end;
};
constexpr Scale MEASURE_SCALE{2, 8, 12, 22, 92, 100};
constexpr Scale TIME_SCALE{2, 5, 7, 12, 55, 100};

class Reporter {
public:
    Reporter(const ProgressFn &fn, const Scale &scale) : m_fn(fn), m_scale(scale) {}

    void at(Stage stage, double percent)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int p = std::clamp(int(std::lround(percent)), m_last, 100);
        if (p == m_last && stage == m_stage)
            return;
        m_last  = p;
        m_stage = stage;
        if (m_fn)
            m_fn(stage, p);
    }
    // Доля 0–1 внутри отрезка [from, to].
    void within(Stage stage, int from, int to, double fraction) { at(stage, from + (to - from) * std::clamp(fraction, 0., 1.)); }

    // Статус Print::process и экспорта G-code.
    void on_status(const PrintBase::SlicingStatus &status, size_t gcode_layers)
    {
        if (status.warning_step != -1 || status.percent < 0)
            return;
        static const std::string gcode_prefix = "Generating G-code: layer ";
        if (status.text.compare(0, gcode_prefix.size(), gcode_prefix) == 0) {
            const long layer = std::strtol(status.text.c_str() + gcode_prefix.size(), nullptr, 10);
            if (gcode_layers > 0)
                within(Stage::GCode, m_scale.after_slice, m_scale.end - 1, double(layer) / double(gcode_layers));
            return;
        }
        if (status.percent >= 80)
            return;
        // 50–70 % у Orca — проверка и построение поддержек.
        const Stage stage = status.percent >= 50 && status.percent < 70 ? Stage::Support : Stage::Slice;
        within(stage, m_scale.slice, m_scale.after_slice, status.percent / 80.);
    }

    const Scale &scale() const { return m_scale; }

private:
    const ProgressFn &m_fn;
    Scale             m_scale;
    std::mutex        m_mutex;
    int               m_last  = 0;
    Stage             m_stage = Stage::Read;
};

BuildVolume build_volume(const DynamicPrintConfig &config)
{
    std::vector<Vec2d>              area = config.opt<ConfigOptionPoints>("printable_area")->values;
    std::vector<std::vector<Vec2d>> extruder_areas;
    std::vector<double>             extruder_heights;
    if (const auto *a = config.opt<ConfigOptionPointsGroups>("extruder_printable_area"))
        extruder_areas = a->values;
    if (const auto *h = config.opt<ConfigOptionFloatsNullable>("extruder_printable_height"))
        extruder_heights = h->values;
    return BuildVolume(area, config.opt_float("printable_height"), extruder_areas, extruder_heights);
}

// Число слоёв, которые выведет G-code, — чтобы «layer N» перевести в проценты.
size_t gcode_layer_count(const Print &print)
{
    std::vector<double> zs;
    for (const PrintObject *object : print.objects()) {
        for (const Layer *l : object->layers())
            zs.push_back(l->print_z);
        for (const SupportLayer *l : object->support_layers())
            zs.push_back(l->print_z);
    }
    std::sort(zs.begin(), zs.end());
    return size_t(std::unique(zs.begin(), zs.end(), [](double a, double b) { return std::abs(a - b) < EPSILON; }) - zs.begin());
}

struct Job {
    WorkDir       dir;
    MergedConfig  merged;
    PreparedModel model;
    Print         print;
    Warnings      warnings;
    size_t        gcode_layers = 0;
};

// Чтение, починка, автоповорот, расстановка. Возвращает конфиг пластины.
DynamicPrintConfig prepare(Job &job, const JobInput &input, Reporter &rep)
{
    const Scale &s = rep.scale();
    rep.at(Stage::Read, 0);
    job.merged = merge_profiles(job.dir, input.machine_json, input.process_json, input.filament_json);
    load_model(job.dir, input, job.model);
    rep.at(Stage::Read, s.read);

    repair_model(job.model, [&](double f) { rep.within(Stage::Repair, s.read, s.repair, f); });
    job.warnings.repaired = job.model.repaired;

    orient_model(job.model, job.merged.config, [&](double f) { rep.within(Stage::Orient, s.repair, s.orient, f); });

    DynamicPrintConfig config = plate_config(job.merged);
    place_model(job.model, config);
    return config;
}

// Всё до конца Print::process.
void slice(Job &job, const JobInput &input, Reporter &rep)
{
    DynamicPrintConfig config = prepare(job, input, rep);
    rep.at(Stage::Slice, rep.scale().slice);

    if (job.model.model.update_print_volume_state(build_volume(config)) == 0)
        throw BridgeError(ErrorCode::SliceFailed, "no object inside the print volume");

    Print &print = job.print;
    print.set_plate_origin(Vec3d::Zero());
    print.set_plate_index(0);
    print.apply(job.model.model, config);
    print.set_no_check_flag(false);
    const std::string &printer_model = config.opt_string("printer_model", true);
    print.is_BBL_printer() = !printer_model.empty() ? printer_model.compare(0, 9, "Bambu Lab") == 0
                                                    : job.merged.printer_name.compare(0, 9, "Bambu Lab") == 0;
    print.set_check_multi_filaments_compatibility(true);

    std::vector<StringObjectException> validation_warnings;
    const StringObjectException        err = print.validate(&validation_warnings);
    if (!err.string.empty())
        throw BridgeError(ErrorCode::SliceFailed, err.string);
    if (print.empty())
        throw BridgeError(ErrorCode::SliceFailed, "nothing to slice");

    print.set_status_callback([&](const PrintBase::SlicingStatus &status) { rep.on_status(status, job.gcode_layers); });
    Model::setExtruderParams(job.merged.config, job.merged.filament_count);
    Model::setPrintSpeedTable(job.merged.config, print.config());
    print.process();
    job.gcode_layers = gcode_layer_count(print);
}

// Исключения Orca → коды обвязки. bad_alloc пропускается наружу как есть.
template<class F> auto guarded(F &&f) -> decltype(f())
{
    try {
        return f();
    } catch (const BridgeError &) {
        throw;
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const SlicingErrors &ex) {
        std::string message;
        for (const SlicingError &e : ex.errors_)
            message += (message.empty() ? "" : "\n") + std::string(e.what());
        throw BridgeError(ErrorCode::SliceFailed, message.empty() ? ex.what() : message);
    } catch (const std::exception &ex) {
        throw BridgeError(ErrorCode::SliceFailed, ex.what());
    }
}

} // namespace

MeasureResult measure(const JobInput &input, const ProgressFn &progress)
{
    return guarded([&] {
        Reporter rep(progress, MEASURE_SCALE);
        Job      job;
        slice(job, input, rep);

        rep.at(Stage::Measure, MEASURE_SCALE.after_slice);
        MeasureResult  out;
        ExtrudedVolume v = extruded_volume(job.print);
        out.part_mm3     = v.part;
        out.support_mm3  = v.support;
        out.density      = job.print.config().filament_density.get_at(0);
        check_layers(job.print, job.warnings);
        out.warnings = job.warnings;
        out.fits_bed = job.model.fits_bed;
        std::copy(std::begin(job.model.size), std::end(job.model.size), out.size);
        rep.at(Stage::Measure, 100);
        return out;
    });
}

PrintTimeResult print_time(const JobInput &input, const ProgressFn &progress)
{
    return guarded([&] {
        Reporter rep(progress, TIME_SCALE);
        Job      job;
        slice(job, input, rep);

        rep.at(Stage::GCode, TIME_SCALE.after_slice);
        // Отдельного вывода G-code в память в Orca нет: файл ложится в рабочую
        // папку (в браузере — MEMFS) и удаляется вместе с ней.
        GCodeProcessorResult result;
        job.print.export_gcode(job.dir.file("plate_1.gcode"), &result, nullptr);

        PrintTimeResult out;
        out.seconds = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time;
        for (const GCodeProcessorResult::MoveVertex &move : result.moves) {
            if (move.type != EMoveType::Extrude || move.internal_only)
                continue;
            const double d   = move.extruder_id < result.filament_diameters.size() ? result.filament_diameters[move.extruder_id] : 1.75;
            const double vol = double(move.delta_extruder) * M_PI * d * d / 4.;
            if (move.extrusion_role == erCustom)
                out.custom_mm3 += vol;
            else if (is_support(move.extrusion_role))
                out.support_mm3 += vol;
            else
                out.model_mm3 += vol;
        }
        out.statistics_json = gcode_statistics_json(result);
        check_layers(job.print, job.warnings);
        out.warnings = job.warnings;
        out.fits_bed = job.model.fits_bed;
        std::copy(std::begin(job.model.size), std::end(job.model.size), out.size);
        rep.at(Stage::GCode, 100);
        return out;
    });
}

PreparedStl prepared_stl(const JobInput &input)
{
    return guarded([&] {
        const ProgressFn none;
        Reporter         rep(none, MEASURE_SCALE);
        Job              job;
        prepare(job, input, rep);
        const std::string path = job.dir.file("prepared.stl");
        if (!store_stl(path.c_str(), &job.model.model, true))
            throw BridgeError(ErrorCode::SliceFailed, "cannot write STL");
        boost::nowide::ifstream in(path, std::ios::binary);
        PreparedStl out;
        out.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        std::copy(std::begin(job.model.size), std::end(job.model.size), out.size);
        out.fits_bed = job.model.fits_bed;
        out.repaired = job.model.repaired;
        return out;
    });
}

} // namespace pf
