// Мост к libslic3r: объём и масса пластика по траекториям (measure)
// и время печати полным путём с G-code (print_time).
#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pf {

// Коды ошибок совпадают с полем `code` у исключений обвязки js/slicer.js.
enum class ErrorCode { BadFile, EmptyModel, SliceFailed, OutOfMemory };

const char *error_code_name(ErrorCode code);

struct BridgeError : std::runtime_error {
    BridgeError(ErrorCode code, const std::string &message) : std::runtime_error(message), code(code) {}
    ErrorCode code;
};

// Этапы хода работы, как их видит обвязка.
enum class Stage { Read, Repair, Orient, Slice, Support, Measure, GCode };

const char *stage_name(Stage stage);

// percent — доля всей работы движка, 0–100.
using ProgressFn = std::function<void(Stage stage, int percent)>;

struct JobInput {
    std::string                file_name; // по расширению и содержимому выбирается формат
    std::vector<std::uint8_t>  data;
    double                     scale = 1.;
    // Развёрнутые системные профили (profiles/a1.json), JSON-текстом, с надстройками
    // поверх профиля процесса: поддержки, «прочная деталь».
    std::string                machine_json;
    std::string                process_json;
    std::string                filament_json;
};

struct Warnings {
    bool repaired       = false; // сетку пришлось чинить
    bool thin_walls     = false; // есть места тоньше линии: в слое остров без единой траектории
    bool floating_parts = false; // между слоями с пластиком пустой промежуток, деталь висит
    std::vector<std::string> codes() const;
};

struct MeasureResult {
    double   part_mm3     = 0.; // модель, кайма, юбка, башня
    double   support_mm3  = 0.; // поддержки
    double   density      = 0.; // г/см³ из профиля пластика
    double   size[3]      = {0., 0., 0.};
    bool     fits_bed     = true;
    Warnings warnings;
    double   part_grams() const { return part_mm3 * density / 1000.; }
    double   support_grams() const { return support_mm3 * density / 1000.; }
};

struct PrintTimeResult {
    double      seconds = 0.;
    // Расход по данным GCodeProcessor, для сверки с measure (без линии прочистки).
    double      model_mm3   = 0.;
    double      support_mm3 = 0.;
    double      custom_mm3  = 0.; // стартовый и прочие G-code профиля
    double      size[3]     = {0., 0., 0.};
    bool        fits_bed    = true;
    Warnings    warnings;
    std::string statistics_json; // gcode_statistics_json(), как у OrcaSlicer --export-statistics
};

// Модель после починки, автоповорота и расстановки — двоичный STL в координатах стола.
// Для сверки: OrcaSlicer на компьютере режет ровно ту же геометрию (--arrange 0).
struct PreparedStl {
    std::vector<std::uint8_t> bytes;
    double                    size[3]  = {0., 0., 0.};
    bool                      fits_bed = true;
    bool                      repaired = false;
};

MeasureResult   measure(const JobInput &input, const ProgressFn &progress);
PrintTimeResult print_time(const JobInput &input, const ProgressFn &progress);
PreparedStl     prepared_stl(const JobInput &input);

// Итоговая конфигурация печати в виде «ключ = значение» по строке, как блок
// настроек G-code OrcaSlicer: для сверки слияния профилей (bench/config-diff.mjs).
std::string full_config_dump(const std::string &machine_json, const std::string &process_json, const std::string &filament_json);

} // namespace pf
