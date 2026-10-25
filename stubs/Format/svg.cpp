// Вместо Format/svg.cpp (OCCT): тела из SVG строятся средствами OCCT.
#include <string>

#include "libslic3r/Format/svg.hpp"

#include "../unsupported.hpp"

namespace Slic3r {

bool load_svg(const char *, Model *, std::string &) { wasm_stub::unsupported("SVG import"); }

} // namespace Slic3r
