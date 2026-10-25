// Предупреждения по готовым слоям, без G-code.
//
// floating_parts — между слоями с пластиком промежуток больше слоя и зазора
// поддержки: деталь или её часть висит в воздухе (то же условие, по которому
// GCode::collect_layers_to_print пишет «empty layers … can't be printed»), или
// на первом слое нет ничего.
// thin_walls — в слое есть остров сечения, внутри которого нет ни одной
// траектории: место тоньше линии сопла, оно не напечатается.
#include "internal.hpp"

#include <libslic3r/ExtrusionEntity.hpp>
#include <libslic3r/ExtrusionEntityCollection.hpp>
#include <libslic3r/Layer.hpp>
#include <libslic3r/Print.hpp>

#include <algorithm>
#include <cmath>

using namespace Slic3r;

namespace pf {
namespace {

// Порог для thin_walls: потерянный объём меньше этого считаем шумом
// (обрезки углов, мелкие острова на кромках).
constexpr double LOST_VOLUME_MM3 = 1.0;
// Острова меньше этой площади не проверяем, мм².
constexpr double MIN_ISLAND_MM2 = 0.05;

bool floating(const PrintObject &object)
{
    const PrintObjectConfig &oc = object.config();
    const double lh  = oc.layer_height.value;
    auto         up  = [lh](double d) { return lh > 0. ? std::ceil(d / lh) * lh : d; };
    const double top = up(oc.support_top_z_distance.value);
    const double bot = oc.support_bottom_z_distance.value == 0. ? top : up(oc.support_bottom_z_distance.value);

    const auto &layers  = object.layers();
    const auto &support = object.support_layers();
    size_t      i = 0, j = 0;
    bool        first = true, last_was_support = false, have_last = false;
    double      last_z = 0.;
    while (i < layers.size() || j < support.size()) {
        // Слой объекта и слой поддержки на одной высоте печатаются вместе.
        const Layer        *ol = nullptr;
        const SupportLayer *sl = nullptr;
        double              z;
        if (j >= support.size() || (i < layers.size() && layers[i]->print_z <= support[j]->print_z + EPSILON)) {
            ol = layers[i++];
            z  = ol->print_z;
            if (j < support.size() && std::abs(support[j]->print_z - z) < EPSILON)
                sl = support[j++];
        } else {
            sl = support[j++];
            z  = sl->print_z;
        }
        const bool   has    = (ol && ol->has_extrusions()) || (sl && sl->has_extrusions());
        const Layer *height = ol ? ol : static_cast<const Layer *>(sl);
        if (first) {
            if (!has)
                return true;
            first = false;
        }
        if ((ol && ol->has_extrusions()) || sl) {
            double gap = sl ? bot : top;
            if (have_last && last_was_support)
                gap = std::max(gap, oc.raft_contact_distance.value);
            const double max_z = (have_last ? last_z : 0.) + height->height + std::max(0., gap);
            if (has && z > max_z + 2. * EPSILON)
                return true;
        }
        if (has) {
            last_z           = z;
            last_was_support = sl != nullptr;
            have_last        = true;
        }
    }
    return false;
}

void collect_points(const ExtrusionEntity &entity, Points &out)
{
    if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
        for (const ExtrusionEntity *child : collection->entities)
            if (child != nullptr)
                collect_points(*child, out);
        return;
    }
    if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity)) {
        if (!path->polyline.points.empty())
            out.push_back(path->polyline.points[path->polyline.points.size() / 2].to_point());
        return;
    }
    if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
        for (const ExtrusionPath &p : multipath->paths)
            collect_points(p, out);
        return;
    }
    if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity))
        for (const ExtrusionPath &p : loop->paths)
            collect_points(p, out);
}

double lost_volume(const PrintObject &object)
{
    const double min_area = MIN_ISLAND_MM2 / (SCALING_FACTOR * SCALING_FACTOR);
    double       lost     = 0.;
    Points       points;
    for (const Layer *layer : object.layers()) {
        points.clear();
        for (const LayerRegion *layerm : layer->regions()) {
            collect_points(layerm->perimeters, points);
            collect_points(layerm->fills, points);
        }
        for (const ExPolygon &island : layer->lslices) {
            const double area = island.area();
            if (area < min_area)
                continue;
            const BoundingBox box = get_extents(island.contour);
            const bool covered = std::any_of(points.begin(), points.end(), [&](const Point &p) { return box.contains(p) && island.contains(p); });
            if (!covered)
                lost += area * SCALING_FACTOR * SCALING_FACTOR * layer->height;
        }
    }
    return lost * double(object.instances().size());
}

} // namespace

void check_layers(const Print &print, Warnings &warnings)
{
    double lost = 0.;
    for (const PrintObject *object : print.objects()) {
        if (floating(*object))
            warnings.floating_parts = true;
        lost += lost_volume(*object);
    }
    if (lost >= LOST_VOLUME_MM3)
        warnings.thin_walls = true;
}

} // namespace pf
