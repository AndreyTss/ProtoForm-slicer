# Зависимости libslic3r. Два источника, как у самого OrcaSlicer:
#  * префикс из deps/build-deps.sh (Boost, Eigen, cereal, CGAL, NLopt, Qhull,
#    libnoise, libjpeg-turbo) и порты zlib и libpng из Emscripten. Их находит
#    find_package благодаря toolchain.cmake префикса;
#  * библиотеки, вложенные в orca/deps_src, через их собственные CMakeLists.
# Имена целей те же, что у OrcaSlicer, поэтому CMakeLists из deps_src
# подключаются без правок.

# deps_src/qhull зовёт её при SLIC3R_STATIC; нужна она только MSVC.
function(slic3r_remap_configs targets from_cfg to_cfg)
endfunction()

# Чужой код компилируется без предупреждений, как у OrcaSlicer.
function(engine_silence_warnings)
    foreach (target IN LISTS ARGN)
        get_target_property(type ${target} TYPE)
        if (NOT type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${target} PRIVATE -w)
        endif ()
    endforeach ()
endfunction()

find_package(Threads REQUIRED)   # его просит mcut

# ---- Boost: те же компоненты и интерфейсные цели, что в CMakeLists OrcaSlicer ----
set(Boost_USE_STATIC_LIBS ON)
set(Boost_NO_SYSTEM_PATHS TRUE)
find_package(Boost 1.83.0 REQUIRED COMPONENTS
    system filesystem thread log log_setup locale regex chrono atomic date_time iostreams program_options nowide)
add_library(boost_headeronly INTERFACE)
add_library(boost_libs INTERFACE)
target_include_directories(boost_headeronly SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
target_link_libraries(boost_libs INTERFACE boost_headeronly ${Boost_LIBRARIES})
if (NOT TARGET Boost::boost)
    add_library(Boost::boost ALIAS boost_headeronly)   # её линкует libnest2d
endif ()

find_package(Eigen3 5.0.1 REQUIRED)
find_package(cereal REQUIRED)
if (NOT TARGET cereal::cereal)
    add_library(cereal::cereal ALIAS cereal)
endif ()
find_package(NLopt 1.4 REQUIRED)    # FindNLopt из orca/cmake/modules
find_package(libnoise REQUIRED)     # Findlibnoise оттуда же

# CGAL: одни заголовки. CGAL_DISABLE_GMP из toolchain.cmake: точные числа
# из Boost.Multiprecision. CMP0167 передаётся как умолчание, потому что
# CGALConfig сам переустанавливает политики (так же делает OrcaSlicer).
cmake_policy(PUSH)
set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
find_package(CGAL 6.2 REQUIRED)
unset(CMAKE_POLICY_DEFAULT_CMP0167)
cmake_policy(POP)

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

# ---- TBB: потоков нет, вместо oneTBB однопоточная замена из shims/tbb-serial ----
add_library(engine_tbb_serial INTERFACE)
target_include_directories(engine_tbb_serial SYSTEM INTERFACE "${REPO_DIR}/shims/tbb-serial")
add_library(TBB::tbb ALIAS engine_tbb_serial)
add_library(TBB::tbbmalloc ALIAS engine_tbb_serial)

# ---- Библиотеки из orca/deps_src, только нужные libslic3r ----
# mcut без своего пула потоков: потоков нет, а нарезке mcut не нужен
# (только булевы операции и разрезы).
set(MCUT_BUILD_WITH_COMPUTE_HELPER_THREADPOOL OFF CACHE BOOL "" FORCE)
set(MCUT_BUILD_AS_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(MCUT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MCUT_BUILD_TUTORIALS OFF CACHE BOOL "" FORCE)
# deps_src/qhull берёт Qhull 8.0.2 из префикса и линкует его статически.
set(SLIC3R_STATIC ON)

set(ENGINE_DEPS_SRC agg ankerl earcut fast_float nanosvg nlohmann libigl
                    admesh clipper2 expat glu-libtess libnest2d mcut miniz qhull qoi semver)
foreach (dir IN LISTS ENGINE_DEPS_SRC)
    add_subdirectory("${ORCA_SRC}/deps_src/${dir}" "${CMAKE_BINARY_DIR}/deps_src/${dir}")
endforeach ()
engine_silence_warnings(admesh Clipper2 expat glu-libtess libnest2d mcut miniz_static qoi semver)

# Как у OrcaSlicer, когда в системе нет expat: вложенная копия.
set(EXPAT_INCLUDE_DIRS "${ORCA_SRC}/deps_src/expat")
set(EXPAT_LIBRARIES expat)
