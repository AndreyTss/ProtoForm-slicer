// Общий помощник заглушек: возможность, которой нет в WebAssembly,
// бросает обычную ошибку libslic3r с понятным текстом.
#pragma once

#include <string>

#include "libslic3r/Exception.hpp"

namespace Slic3r::wasm_stub {

[[noreturn]] inline void unsupported(const std::string &what)
{
    throw Slic3r::RuntimeError(what + ": not available in the WebAssembly build");
}

} // namespace Slic3r::wasm_stub
