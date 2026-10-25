// Объём выдавленного пластика по траекториям готовой нарезки, без G-code.
//
// Повторяет то, как GCode::_extrude считает E (src/libslic3r/GCode.cpp):
//   E/мм = mm3_per_mm × print_flow_ratio × filament_flow_ratio × множитель роли / сечение нити,
// значит объём = длина × mm3_per_mm × print_flow_ratio × filament_flow_ratio × множитель роли.
// Длина — как у G-code: дуги после arc fitting по длине дуги, отрезки короче EPSILON
// не выдавливаются, замкнутые линии укорачиваются на seam_gap (GCode::extrude_loop).
// Стартовый G-code (линия прочистки сопла) сюда не попадает: это не траектории.
#include "internal.hpp"

#include <libslic3r/ExtrusionEntity.hpp>
#include <libslic3r/ExtrusionEntityCollection.hpp>
#include <libslic3r/Flow.hpp>
#include <libslic3r/Layer.hpp>
#include <libslic3r/Print.hpp>

#include <cmath>

using namespace Slic3r;

namespace pf {
namespace {

// Множители потока из конфигурации, которую GCode держит в m_config при печати
// этих траекторий: печать + объект + регион.
struct FlowFactors {
    double base = 1.;
    double top = 1., bottom = 1., internal_bridge = 1., brim = 1.;
    bool   other = false;
    double outer = 1., inner = 1., overhang = 1., sparse = 1., solid = 1., gap = 1., support = 1., support_interface = 1., first_layer = 1.;

    FlowFactors(const PrintConfig &pc, const PrintObjectConfig &oc, const PrintRegionConfig &rc)
    {
        base              = rc.print_flow_ratio.value * pc.filament_flow_ratio.get_at(0);
        top               = rc.top_solid_infill_flow_ratio.get_at(0);
        bottom            = rc.bottom_solid_infill_flow_ratio.value;
        internal_bridge   = rc.internal_bridge_flow.value;
        brim              = oc.brim_flow_ratio.value;
        other             = oc.set_other_flow_ratios.value;
        outer             = rc.outer_wall_flow_ratio.value;
        inner             = rc.inner_wall_flow_ratio.value;
        overhang          = rc.overhang_flow_ratio.value;
        sparse            = rc.sparse_infill_flow_ratio.value;
        solid             = rc.internal_solid_infill_flow_ratio.value;
        gap               = rc.gap_fill_flow_ratio.value;
        support           = oc.support_flow_ratio.value;
        support_interface = oc.support_interface_flow_ratio.value;
        first_layer       = rc.first_layer_flow_ratio.value;
    }

    // Порядок условий — как в GCode::_extrude.
    double of(ExtrusionRole role, bool first_layer_path) const
    {
        double f = base;
        if (role == erTopSolidInfill)
            f *= top;
        else if (role == erBottomSurface)
            f *= bottom;
        else if (role == erInternalBridgeInfill)
            f *= internal_bridge;
        else if (role == erBrim)
            f *= brim;
        if (other) {
            switch (role) {
            case erExternalPerimeter: f *= outer; break;
            case erPerimeter: f *= inner; break;
            case erOverhangPerimeter: f *= overhang; break;
            case erInternalInfill: f *= sparse; break;
            case erSolidInfill: f *= solid; break;
            case erGapFill: f *= gap; break;
            case erSupportMaterial: f *= support; break;
            case erSupportMaterialInterface: f *= support_interface; break;
            default: break;
            }
            if (first_layer_path && role != erBrim && role != erSkirt)
                f *= first_layer;
        }
        return f;
    }
};

struct Context {
    const FlowFactors &factors;
    bool               first_layer;
    bool               arc_fitting;
    bool               loop_clipping;
    double             seam_gap_scaled;
};

// Длина пути так, как её проходит G-code (GCode::_extrude, обе ветки).
double extruded_length(const ExtrusionPath &path, bool arc_fitting)
{
    double length = 0.;
    const Polyline3 &pl = path.polyline;
    if (arc_fitting && !pl.fitting_result.empty() && !path.z_contoured) {
        for (const PathFittingData &fit : pl.fitting_result) {
            if (fit.path_type == EMovePathType::Linear_move) {
                for (size_t i = fit.start_point_index + 1; i < fit.end_point_index + 1 && i < pl.points.size(); ++i) {
                    const double l = Line(pl.points[i - 1].to_point(), pl.points[i].to_point()).length() * SCALING_FACTOR;
                    if (l >= EPSILON)
                        length += l;
                }
            } else if (fit.path_type == EMovePathType::Arc_move_cw || fit.path_type == EMovePathType::Arc_move_ccw) {
                const double l = fit.arc_data.length * SCALING_FACTOR;
                if (l >= EPSILON)
                    length += l;
            }
        }
        return length;
    }
    for (const Line3 &line : pl.lines()) {
        const double l = line.length() * SCALING_FACTOR;
        if (l >= EPSILON)
            length += l;
    }
    return length;
}

double path_volume(const ExtrusionPath &path, const Context &ctx)
{
    if (path.is_force_no_extrusion() || path.mm3_per_mm <= 0.)
        return 0.;
    return extruded_length(path, ctx.arc_fitting) * path.mm3_per_mm * ctx.factors.of(path.role(), ctx.first_layer);
}

double entity_volume(const ExtrusionEntity &entity, const Context &ctx)
{
    if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
        double v = 0.;
        for (const ExtrusionEntity *child : collection->entities)
            if (child != nullptr)
                v += entity_volume(*child, ctx);
        return v;
    }
    if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity))
        return path_volume(*path, ctx);
    if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
        double v = 0.;
        for (const ExtrusionPath &p : multipath->paths)
            v += path_volume(p, ctx);
        return v;
    }
    if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity)) {
        // GCode::extrude_loop: конец петли срезается на seam_gap, чтобы сопло не
        // наезжало на начало шва. Где начнётся шов, на длину не влияет.
        ExtrusionPaths paths;
        loop->clip_end(ctx.loop_clipping ? ctx.seam_gap_scaled : 0., &paths);
        double v = 0.;
        for (const ExtrusionPath &p : paths)
            v += path_volume(p, ctx);
        return v;
    }
    return 0.;
}

bool is_first_layer(const Layer &layer) { return layer.id() == 0 && std::abs(layer.bottom_z()) < EPSILON; }

double seam_gap_scaled(const PrintRegionConfig &rc, const PrintConfig &pc)
{
    return scale_(rc.seam_gap.get_abs_value(pc.nozzle_diameter.get_at(0)));
}

// Наклонный шов (scarf) G-code печатает без среза и с переменным потоком; при нём
// считаем петлю целиком. В профилях A1 он выключен (seam_slope_type = none).
bool loop_clipping(const PrintRegionConfig &rc, const PrintConfig &pc)
{
    return !pc.spiral_mode.value && rc.seam_slope_type.value == SeamScarfType::None;
}

// Юбка: GCode::generate_skirt печатает петли на первом слое и на skirt_height слоях
// (или на всех при draft_shield) с потоком skirt_flow под высоту слоя. Приближение:
// высоты берутся у слоёв первого объекта. В профилях A1 юбки нет (skirt_loops = 0).
double skirt_volume(const Print &print)
{
    std::vector<const ExtrusionEntityCollection *> skirts;
    for (const Print::SkirtBrimGroup &group : print.skirt_brim_groups())
        if (!group.skirt.empty())
            skirts.push_back(&group.skirt);
    if (skirts.empty() && !print.skirt().empty())
        skirts.push_back(&print.skirt());
    if (skirts.empty() || print.objects().empty())
        return 0.;

    const PrintConfig &pc     = print.config();
    const auto        &layers = print.objects().front()->layers();
    size_t             count  = std::min<size_t>(layers.size(), size_t(std::max(pc.skirt_height.value, 1)));
    if (pc.draft_shield.value == dsEnabled)
        count = layers.size();
    const FlowFactors factors(pc, print.default_object_config(), print.default_region_config());

    double v = 0.;
    for (size_t li = 0; li < count; ++li) {
        const double mm3   = print.skirt_flow().with_height(float(layers[li]->height)).mm3_per_mm();
        const Context ctx{factors, li == 0, false, true, seam_gap_scaled(print.default_region_config(), pc)};
        for (const ExtrusionEntityCollection *skirt : skirts) {
            size_t first = 0;
            if (li > 0 && pc.single_loop_draft_shield.value && !skirt->entities.empty())
                first = skirt->entities.size() - 1;
            for (size_t i = first; i < skirt->entities.size(); ++i) {
                const auto *loop = dynamic_cast<const ExtrusionLoop *>(skirt->entities[i]);
                if (loop == nullptr)
                    continue;
                ExtrusionLoop copy = *loop;
                for (ExtrusionPath &p : copy.paths)
                    p.mm3_per_mm = mm3;
                v += entity_volume(copy, ctx);
            }
        }
    }
    return v;
}

} // namespace

ExtrudedVolume extruded_volume(const Print &print)
{
    ExtrudedVolume     out;
    const PrintConfig &pc          = print.config();
    const bool         arc_fitting = pc.enable_arc_fitting.value;

    for (const PrintObject *object : print.objects()) {
        const double copies = double(object->instances().size());
        const PrintObjectConfig &oc = object->config();

        for (const Layer *layer : object->layers()) {
            const bool first = is_first_layer(*layer);
            for (const LayerRegion *layerm : layer->regions()) {
                const PrintRegionConfig &rc = layerm->region().config();
                const FlowFactors        factors(pc, oc, rc);
                const Context ctx{factors, first, arc_fitting, loop_clipping(rc, pc), seam_gap_scaled(rc, pc)};
                // thin_fills здесь не считаем: при заполнении они уже скопированы в fills.
                out.part += copies * (entity_volume(layerm->perimeters, ctx) + entity_volume(layerm->fills, ctx));
            }
        }

        // Поддержки печатаются с настройками региона по умолчанию поверх объекта.
        const PrintRegionConfig &rc = print.default_region_config();
        const FlowFactors        factors(pc, oc, rc);
        for (const SupportLayer *layer : object->support_layers()) {
            const Context ctx{factors, is_first_layer(*layer), arc_fitting, loop_clipping(rc, pc), seam_gap_scaled(rc, pc)};
            out.support += copies * entity_volume(layer->support_fills, ctx);
        }
    }

    // Кайма: каждая из групп печатается один раз (GCode::generate_object_brim).
    {
        const FlowFactors factors(pc, print.default_object_config(), print.default_region_config());
        const Context ctx{factors, true, arc_fitting, loop_clipping(print.default_region_config(), pc),
                          seam_gap_scaled(print.default_region_config(), pc)};
        for (const Print::SkirtBrimGroup &group : print.skirt_brim_groups())
            for (const Print::SkirtBrimGroup::Brim &brim : group.brims)
                out.part += entity_volume(brim.brim, ctx);
    }

    out.part += skirt_volume(print);

    // Башня очистки: при одном пластике её нет, но если включена (таймлапс), её
    // расход в мм нити известен после нарезки.
    if (print.has_wipe_tower()) {
        const double r = pc.filament_diameter.get_at(0) / 2.;
        for (float mm : print.wipe_tower_data().used_filament)
            out.part += double(mm) * M_PI * r * r;
    }
    return out;
}

} // namespace pf
