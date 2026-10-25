// Проверка сборки: libslic3r в WebAssembly режет кубик 20 мм и пишет G-code.
// Модель строится из треугольников в памяти, настройки — умолчания libslic3r.
// Запись G-code проходит через конвейер TBB (замена без потоков) и
// GCodeProcessor. Код возврата 0, если есть слои и G-code.
#include <cstdio>
#include <exception>

#include <libslic3r/GCode/GCodeProcessor.hpp>
#include <libslic3r/Layer.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/TriangleMesh.hpp>

int main()
{
    using namespace Slic3r;
    try {
        Model        model;
        ModelObject *object = model.add_object();
        object->name        = "cube";
        object->add_volume(make_cube(20., 20., 20.));
        object->add_instance();
        model.center_instances_around_point(Vec2d(100., 100.));
        object->ensure_on_bed();

        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        // Иначе validate() ругается на относительную подачу без сброса E.
        config.set_deserialize_strict("layer_change_gcode", "G92 E0");

        Print print;
        print.apply(model, config);
        StringObjectException error = print.validate();
        if (!error.string.empty())
            std::printf("validate: %s\n", error.string.c_str());
        print.process();

        const PrintObject *po = print.objects().front();
        std::printf("layers: %zu\n", po->layer_count());
        std::printf("first layer z: %.3f, last layer z: %.3f\n",
                    po->get_layer(0)->print_z, po->get_layer(int(po->layer_count()) - 1)->print_z);

        GCodeProcessorResult result;
        print.export_gcode("/tmp/cube.gcode", &result);
        const PrintStatistics &stats = print.print_statistics();
        std::printf("gcode: filament %.1f mm, time %s\n", stats.total_used_filament,
                    stats.estimated_normal_print_time.c_str());
        return po->layer_count() > 0 && stats.total_used_filament > 0. ? 0 : 1;
    } catch (const std::exception &e) {
        std::printf("error: %s\n", e.what());
        return 2;
    }
}
