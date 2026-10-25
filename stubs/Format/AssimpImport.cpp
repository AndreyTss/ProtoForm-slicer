// Вместо Format/AssimpImport.cpp (Assimp: glTF, GLB, FBX с текстурами).
#include "libslic3r/Format/AssimpImport.hpp"

#include "../unsupported.hpp"

namespace Slic3r {

bool load_assimp_textured_model(const std::string &, TexturedMesh &, std::string *)
{
    wasm_stub::unsupported("glTF/GLB/FBX import");
}

} // namespace Slic3r
