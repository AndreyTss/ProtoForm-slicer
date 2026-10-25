# libslic3r из OrcaSlicer в WebAssembly (Emscripten, один поток) и модуль
# движка engine.mjs + engine.wasm из engine/bridge.
#
# Настройка (внутри контейнера, это делает docker/build.sh):
#   emcmake cmake -C $WORK/prefix/share/slicer-deps/toolchain.cmake -G Ninja \
#       -S engine -B $WORK/build-engine
#
# Каталог orca/ не меняется: список исходников читается из CMakeLists
# OrcaSlicer, лишнее исключается, правки делаются в копиях (cmake/OrcaSources.cmake).
cmake_minimum_required(VERSION 3.24)
project(slicer_engine LANGUAGES C CXX)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "Движок собирается только Emscripten: настраивайте через emcmake.")
endif ()

set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(REPO_DIR "${ENGINE_DIR}/.." ABSOLUTE)
set(ORCA_SRC "${REPO_DIR}/orca" CACHE PATH "Исходники OrcaSlicer")
option(ENGINE_PCH "pchheader.hpp предкомпилирован, как у OrcaSlicer" ON)
set(ENGINE_EXTRA_LINK_OPTIONS "" CACHE STRING "Дополнительные флаги компоновки движка (список через ;)")

if (NOT EXISTS "${ORCA_SRC}/src/libslic3r/CMakeLists.txt")
    message(FATAL_ERROR "В ${ORCA_SRC} нет исходников OrcaSlicer: подмодуль orca не скачан?")
endif ()
list(PREPEND CMAKE_MODULE_PATH "${ENGINE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${ORCA_SRC}/cmake/modules")   # FindNLopt, Findlibnoise

# Все файлы обязаны компилироваться с флагами, с которыми собран префикс:
# иначе объекты с разной моделью исключений не скомпонуются, а ошибка
# вылезет только в самом конце.
if (NOT DEFINED SLICER_DEPS_PREFIX)
    message(FATAL_ERROR "Нужен -C <префикс>/share/slicer-deps/toolchain.cmake из deps/build-deps.sh.")
endif ()
foreach (flag -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -DBOOST_HAS_PTHREADS -DCGAL_ALWAYS_ROUND_TO_NEAREST)
    string(FIND " ${CMAKE_CXX_FLAGS} " " ${flag} " pos)
    if (pos EQUAL -1)
        message(FATAL_ERROR "В CMAKE_CXX_FLAGS нет ${flag}: настраивайте с toolchain.cmake префикса.")
    endif ()
endforeach ()

# ---- Общие настройки, как в CMakeLists OrcaSlicer (сборка Release без окна) ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

add_compile_options(-fsigned-char)
add_compile_definitions(
    "BBL_RELEASE_TO_PUBLIC=$<CONFIG:Release>"
    "$<$<CONFIG:Release>:WXINSPECTOR_DISABLE>"
    "$<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>"
    wxUSE_UNICODE _UNICODE UNICODE WXINTL_NO_GETTEXT_MACRO wxNO_UNSAFE_WXSTRING_CONV)
# SLIC3R_GUI, SLIC3R_CAD и SLIC3R_PROFILE не заданы, как в сборке без окна.

# Предупреждения как у OrcaSlicer, но без -Werror: новый clang под wasm32
# находит новое в коде, который мы не правим.
add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
    -Wno-reorder -Wno-sign-compare -Wno-misleading-indentation -Wno-switch -Wno-unused-function
    -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label -Wno-unused-local-typedefs
    -Wno-deprecated-declarations -Wno-ignored-attributes)

include_directories(SYSTEM "${ORCA_SRC}/src")

# Пути этой машины в __FILE__ заменяются постоянными: результат не зависит
# от того, где лежат исходники и дерево сборки.
add_compile_options(
    "-ffile-prefix-map=${REPO_DIR}=/src"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=/build")

include(Dependencies)
include(Libslic3r)
include(Engine)
