// Общие части моста, не видимые обвязке.
#pragma once

#include "bridge.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <string>

namespace Slic3r {
class Print;
}

namespace pf {

// Рабочая папка задания в файловой системе процесса (в браузере — MEMFS, то есть
// оперативная память). Loaders Orca читают только файлы, поэтому вход и профили
// кладутся сюда. Папка удаляется в деструкторе.
class WorkDir {
public:
    WorkDir();
    ~WorkDir();
    WorkDir(const WorkDir &) = delete;
    WorkDir &operator=(const WorkDir &) = delete;
    const std::string &path() const { return m_path; }
    std::string        file(const std::string &name) const { return m_path + "/" + name; }
    void               write(const std::string &name, const void *data, size_t size) const;

private:
    std::string m_path;
};

// config.cpp: слияние машины, процесса и пластика, как CLI::run в OrcaSlicer.cpp.
struct MergedConfig {
    Slic3r::DynamicPrintConfig config;
    std::string                printer_name;
    int                        extruder_count = 1;
    int                        filament_count = 1;
};
MergedConfig merge_profiles(const WorkDir &dir, const std::string &machine_json, const std::string &process_json,
                            const std::string &filament_json);
// Конфигурация отдельной пластины поверх общей (OrcaSlicer.cpp, перед Print::apply).
Slic3r::DynamicPrintConfig plate_config(const MergedConfig &merged);

// model.cpp: чтение файла, масштаб, починка, автоповорот, расстановка на столе.
struct PreparedModel {
    Slic3r::Model model;
    double        size[3]  = {0., 0., 0.};
    bool          fits_bed = true;
    bool          repaired = false;
};
void load_model(const WorkDir &dir, const JobInput &input, PreparedModel &out);
void repair_model(PreparedModel &model, const std::function<void(double)> &progress);
void orient_model(PreparedModel &model, const Slic3r::DynamicPrintConfig &config, const std::function<void(double)> &progress);
// Ставит всё на стол из config; если не помещается — расширяет стол в config,
// чтобы нарезка всё равно прошла, и отмечает fits_bed = false.
void place_model(PreparedModel &model, Slic3r::DynamicPrintConfig &config);

// volume.cpp: выдавленный объём по траекториям, без G-code.
struct ExtrudedVolume {
    double part    = 0.;
    double support = 0.;
};
ExtrudedVolume extruded_volume(const Slic3r::Print &print);

// checks.cpp: предупреждения по готовым слоям.
void check_layers(const Slic3r::Print &print, Warnings &warnings);

} // namespace pf
