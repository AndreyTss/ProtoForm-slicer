// Модель: чтение файла, масштаб, починка сетки, автоповорот, расстановка на столе.
#include "internal.hpp"

#include <libslic3r/BuildVolume.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/MeshRepair.hpp>
#include <libslic3r/Orient.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/Semver.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <new>

using namespace Slic3r;

namespace pf {
namespace {

std::string lower_extension(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    std::string  ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

// Формат по содержимому, имя — подсказка: 3MF — это ZIP, OBJ отличаем по расширению,
// всё остальное читаем как STL (двоичный или текстовый — решит admesh).
std::string detect_format(const JobInput &input)
{
    const auto &d = input.data;
    if (d.size() >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4)
        return "3mf";
    const std::string ext = lower_extension(input.file_name);
    if (ext == "3mf")
        throw BridgeError(ErrorCode::BadFile, "3MF is not a ZIP archive");
    if (ext == "obj")
        return "obj";
    return "stl";
}

size_t model_part_facets(const Model &model)
{
    size_t facets = 0;
    for (const ModelObject *object : model.objects)
        for (const ModelVolume *volume : object->volumes)
            if (volume->is_model_part())
                facets += volume->mesh().facets_count();
    return facets;
}

// Набор «минимум поддержек» кнопки «Auto orient» (slic3r/GUI/Jobs/OrientJob.cpp,
// setMinimalSupportAreaPrams; GLCanvas3D::OrientSettings::min_area = true по умолчанию).
orientation::OrientParams gui_orient_params()
{
    orientation::OrientParams p;
    p.TAR_A                           = 0.015f;
    p.TAR_B                           = 0.177f;
    p.RELATIVE_F                      = 20;
    p.CONTOUR_F                       = 0.5f;
    p.BOTTOM_F                        = 2.5f;
    p.BOTTOM_HULL_F                   = 0.1f;
    p.TAR_C                           = 0.1f;
    p.TAR_D                           = 1;
    p.TAR_E                           = 0.0115f;
    p.FIRST_LAY_H                     = 0.2f;
    p.VECTOR_TOL                      = -0.00083f;
    p.NEGL_FACE_SIZE                  = 0.01f;
    p.ASCENT                          = -0.5f;
    p.PLAFOND_ADV                     = 0.0599f;
    p.CONTOUR_AMOUNT                  = 0.0182427f;
    p.OV_H                            = 2.574f;
    p.height_offset                   = 2.3728f;
    p.height_log                      = 0.041375f;
    p.height_log_k                    = 1.9325457f;
    p.LAF_MAX                         = 0.999f;
    p.LAF_MIN                         = 0.97f;
    p.TAR_LAF                         = 0.001f;
    p.TAR_PROJ_AREA                   = 0.1f;
    p.BOTTOM_MIN                      = 0.1f;
    p.BOTTOM_MAX                      = 2000;
    p.height_to_bottom_hull_ratio_MIN = 1;
    p.BOTTOM_HULL_MAX                 = 2000;
    p.APPERANCE_FACE_SUPP             = 3;
    p.overhang_angle                  = 60.f;
    p.use_low_angle_face              = true;
    p.min_volume                      = false;
    p.fun_dir                         = {};
    p.parallel                        = true;
    p.progressind                     = {};
    p.stopcondition                   = {};
    return p;
}

BoundingBoxf bed_box(const DynamicPrintConfig &config)
{
    BoundingBoxf box;
    for (const Vec2d &p : config.opt<ConfigOptionPoints>("printable_area")->values)
        box.merge(p);
    return box;
}

BoundingBoxf3 instance_box(const ModelObject &object, size_t i) { return object.instance_bounding_box(i); }

BoundingBoxf3 model_box(const Model &model)
{
    BoundingBoxf3 box;
    for (const ModelObject *object : model.objects)
        for (size_t i = 0; i < object->instances.size(); ++i)
            box.merge(instance_box(*object, i));
    return box;
}

void move_instance_xy(ModelObject &object, size_t i, const Vec2d &target_min)
{
    const BoundingBoxf3 box    = instance_box(object, i);
    ModelInstance      &inst   = *object.instances[i];
    Vec3d               offset = inst.get_offset();
    offset.x() += target_min.x() - box.min.x();
    offset.y() += target_min.y() - box.min.y();
    inst.set_offset(offset);
}

struct Slot {
    ModelObject *object;
    size_t       instance;
    Vec2d        size;
    Vec2d        at; // левый нижний угол после раскладки, от начала раскладки
};

// Раскладка полками: ряды слева направо, ряды друг за другом по Y. Простая и
// предсказуемая; задача — не наложить детали друг на друга и оставить место
// для каймы и поддержек, а не уплотнить стол.
Vec2d shelf_pack(std::vector<Slot> &slots, double width, double gap)
{
    std::sort(slots.begin(), slots.end(), [](const Slot &a, const Slot &b) { return a.size.y() > b.size.y(); });
    double x = 0., y = 0., row = 0., used_w = 0.;
    for (Slot &s : slots) {
        if (x > 0. && x + s.size.x() > width) {
            y += row + gap;
            x = row = 0.;
        }
        s.at = Vec2d(x, y);
        x += s.size.x() + gap;
        row    = std::max(row, s.size.y());
        used_w = std::max(used_w, x - gap);
    }
    return Vec2d(used_w, y + row);
}

void set_bed(DynamicPrintConfig &config, double width, double depth, double height)
{
    config.opt<ConfigOptionPoints>("printable_area", true)->values = {Vec2d(0, 0), Vec2d(width, 0), Vec2d(width, depth), Vec2d(0, depth)};
    config.opt<ConfigOptionFloat>("printable_height", true)->value = height;
    config.opt<ConfigOptionPoints>("bed_exclude_area", true)->values.clear();
}

} // namespace

void load_model(const WorkDir &dir, const JobInput &input, PreparedModel &out)
{
    if (input.data.empty())
        throw BridgeError(ErrorCode::BadFile, "empty file");
    if (!(input.scale > 0.) || !std::isfinite(input.scale))
        throw BridgeError(ErrorCode::BadFile, "invalid scale");

    const std::string format = detect_format(input);
    const std::string file   = "model." + format;
    dir.write(file, input.data.data(), input.data.size());

    DynamicPrintConfig        file_config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::EnableSilent);
    PlateDataPtrs             plate_data;
    std::vector<Preset *>     project_presets;
    bool                      is_bbl_3mf = false;
    Semver                    file_version;
    auto                      release = [&]() {
        release_PlateData_list(plate_data);
        for (Preset *p : project_presets)
            delete p;
        project_presets.clear();
    };
    try {
        // Как CLI для файлов не из Bambu Studio: только геометрия, экземпляры по умолчанию.
        out.model = Model::read_from_file(dir.file(file), &file_config, &substitutions,
                                          LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances, &plate_data,
                                          &project_presets, &is_bbl_3mf, &file_version);
        release();
        // Чтение 3MF заводит временную папку резервной копии. Деструктор Model
        // отдал бы её менеджеру копий окна программы, а в WebAssembly это
        // обрывает работу (std::terminate). Удаляем папку сами и отвязываем путь.
        try {
            out.model.remove_backup_path_if_exist();
        } catch (const std::exception &) {
            out.model.set_backup_path("detach");
        }
    } catch (const std::bad_alloc &) {
        release();
        throw;
    } catch (const std::exception &ex) {
        release();
        throw BridgeError(ErrorCode::BadFile, ex.what());
    }

    if (out.model.objects.empty() || model_part_facets(out.model) == 0)
        throw BridgeError(ErrorCode::EmptyModel, "no solid bodies");

    for (ModelObject *object : out.model.objects) {
        // Свои настройки объекта из 3MF не применяем: всё режется одним выбранным профилем.
        object->config.reset();
        object->layer_config_ranges.clear();
        object->layer_height_profile.clear();
        for (ModelVolume *volume : object->volumes)
            volume->config.reset();
        if (object->instances.empty())
            object->add_instance();
        if (input.scale != 1.)
            object->scale(Vec3d(input.scale, input.scale, input.scale));
        object->invalidate_bounding_box();
        object->ensure_on_bed();
    }
}

void repair_model(PreparedModel &model, const std::function<void(double)> &progress)
{
    // В 2.5.0-alpha сетка при чтении не чинится. Чиним только то, что явно сломано
    // (дыры, вывернутая наружу сетка): repair_mesh на целой сетке ничего не меняет,
    // но долго проверяет самопересечения.
    std::vector<ModelVolume *> broken;
    for (ModelObject *object : model.model.objects)
        for (ModelVolume *volume : object->volumes)
            if (volume->is_model_part()) {
                const TriangleMeshStats &stats = volume->mesh().stats();
                if (stats.open_edges > 0 || stats.volume < 0.f)
                    broken.push_back(volume);
            }
    for (size_t i = 0; i < broken.size(); ++i) {
        MeshRepairStats stats;
        std::string     error;
        const double    base = double(i) / double(broken.size());
        const bool      ok   = broken[i]->repair(false, &stats, &error, [&](int percent) {
            if (progress)
                progress(base + percent / 100. / double(broken.size()));
        });
        if (ok && stats.modified) {
            model.repaired = true;
            broken[i]->get_object()->invalidate_bounding_box();
        }
    }
    if (progress)
        progress(1.);
}

void orient_model(PreparedModel &model, const DynamicPrintConfig &config, const std::function<void(double)> &progress)
{
    using orientation::OrientMesh;
    orientation::OrientParams params = gui_orient_params();

    // OrientJob::get_orient_mesh: угол нависания из support_threshold_angle,
    // поворот вокруг Z к обдуву, только если он есть.
    Vec3d cooling_direction(0, 0, 0);
    bool  has_cooling_fan = false;
    if (config.has("fan_direction") && config.has("auxiliary_fan")) {
        const FanDirection dir = config.option<ConfigOptionEnum<FanDirection>>("fan_direction")->value;
        if (dir != FanDirection::fdUndefine && config.opt_bool("auxiliary_fan")) {
            cooling_direction = dir == FanDirection::fdRight ? Vec3d(1, 0, 0) : Vec3d(-1, 0, 0);
            has_cooling_fan   = true;
        }
    }
    const double overhang_angle = config.opt_int("support_threshold_angle");

    std::vector<OrientMesh> meshes;
    for (ModelObject *object : model.model.objects) {
        OrientMesh om;
        om.name              = object->name;
        om.mesh              = object->mesh();
        om.overhang_angle    = overhang_angle;
        om.cooling_direction = cooling_direction;
        om.has_cooling_fan   = has_cooling_fan;
        meshes.push_back(std::move(om));
    }
    size_t done = 0;
    params.progressind = [&](unsigned, std::string) {
        if (progress)
            progress(double(done++) / double(std::max<size_t>(meshes.size(), 1)));
    };
    orientation::orient(meshes, {}, params);

    for (size_t i = 0; i < meshes.size(); ++i) {
        ModelObject *object = model.model.objects[i];
        for (ModelInstance *instance : object->instances) {
            instance->rotate(meshes[i].rotation_matrix);
            instance->rotate(meshes[i].rotation_matrix_vertical);
        }
        object->invalidate_bounding_box();
        object->ensure_on_bed();
    }
    if (progress)
        progress(1.);
}

void place_model(PreparedModel &model, DynamicPrintConfig &config)
{
    const BoundingBoxf bed    = bed_box(config);
    const double       bed_w  = bed.size().x();
    const double       bed_d  = bed.size().y();
    const double       bed_h  = config.opt_float("printable_height");
    constexpr double   eps    = 1e-3;
    constexpr double   gap    = 15.; // кайма 5 мм и ветви поддержек с обеих сторон
    constexpr double   margin = 10.;

    std::vector<Slot> slots;
    double            max_h = 0.;
    for (ModelObject *object : model.model.objects)
        for (size_t i = 0; i < object->instances.size(); ++i) {
            const BoundingBoxf3 box = instance_box(*object, i);
            slots.push_back({object, i, Vec2d(box.size().x(), box.size().y()), Vec2d::Zero()});
            max_h = std::max(max_h, box.size().z());
        }

    Vec2d packed = slots.size() == 1 ? slots.front().size : shelf_pack(slots, bed_w, gap);
    model.fits_bed = packed.x() <= bed_w + eps && packed.y() <= bed_d + eps && max_h <= bed_h + eps;

    Vec2d origin = bed.min;
    if (!model.fits_bed) {
        // Деталь больше стола: масса всё равно нужна, режем на расширенном столе.
        const double width = std::max(bed_w, packed.x() + 2 * margin);
        if (slots.size() > 1)
            packed = shelf_pack(slots, width - 2 * margin, gap);
        set_bed(config, std::max(width, packed.x() + 2 * margin), std::max(bed_d, packed.y() + 2 * margin), std::max(bed_h, max_h + margin));
        origin = Vec2d::Zero();
    }
    const BoundingBoxf placed_bed = bed_box(config);
    // Раскладка по центру стола.
    const Vec2d start = origin + (placed_bed.size() - packed) / 2.;
    for (Slot &s : slots)
        move_instance_xy(*s.object, s.instance, start + (slots.size() == 1 ? Vec2d::Zero() : s.at));
    for (ModelObject *object : model.model.objects) {
        object->invalidate_bounding_box();
        object->ensure_on_bed();
    }

    const BoundingBoxf3 all = model_box(model.model);
    for (int a = 0; a < 3; ++a)
        model.size[a] = all.size()[a];
}

} // namespace pf
