// Вместо Format/STEP.cpp (OCCT). Model::read_from_step создаёт Step и зовёт
// load() и mesh(), поэтому класс должен существовать; чтение STEP недоступно.
#include "libslic3r/Format/STEP.hpp"

#include "../unsupported.hpp"

namespace Slic3r {

Step::Step(std::string path, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn)
    : m_path(std::move(path)), m_stepFn(std::move(stepFn)), m_utf8Fn(std::move(isUtf8Fn))
{}

Step::Step(boost::filesystem::path path, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn)
    : Step(path.string(), std::move(stepFn), std::move(isUtf8Fn))
{}

Step::~Step() = default;

Step::Step_Status Step::load() { wasm_stub::unsupported("STEP import"); }

Step::Step_Status Step::mesh(Model *, bool &, bool, double, double) { wasm_stub::unsupported("STEP import"); }

} // namespace Slic3r
