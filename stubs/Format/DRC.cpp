// Вместо Format/DRC.cpp (Draco).
#include "libslic3r/Format/DRC.hpp"

#include "../unsupported.hpp"

namespace Slic3r {

bool load_drc(const char *, TriangleMesh *) { wasm_stub::unsupported("Draco import"); }
bool load_drc(const char *, Model *, const char *) { wasm_stub::unsupported("Draco import"); }

} // namespace Slic3r
