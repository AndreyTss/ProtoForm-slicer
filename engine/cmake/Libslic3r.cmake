# Статические библиотеки libslic3r и libslic3r_cgal из orca/src/libslic3r
# с настройками его CMakeLists, без частей рабочего стола (OrcaSources.cmake)
# и с заглушками из stubs/ и shims/.

include(OrcaSources)

set(LIBSLIC3R_DIR "${ORCA_SRC}/src/libslic3r")
set(LIBSLIC3R_GEN_DIR "${CMAKE_BINARY_DIR}/libslic3r")

# libslic3r_version.h так же, как его делает OrcaSlicer: версия попадает
# в заголовок G-кода и должна совпадать с version.inc.
include("${ORCA_SRC}/version.inc")
if (NOT DEFINED ORCA_CHECK_GCODE_PLACEHOLDERS)
    set(ORCA_CHECK_GCODE_PLACEHOLDERS "0")
endif ()
configure_file("${LIBSLIC3R_DIR}/libslic3r_version.h.in" "${LIBSLIC3R_GEN_DIR}/libslic3r_version.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ORCA_SRC}/version.inc")

orca_drainhole_source("${LIBSLIC3R_GEN_DIR}/SLA/DrainHole.cpp")

# ---- Список исходников ---------------------------------------------------------
orca_libslic3r_sources(orca_all)
set(orca_kept ${orca_all})
foreach (excluded IN LISTS ORCA_LIBSLIC3R_EXCLUDED)
    if (NOT excluded IN_LIST orca_all)
        message(WARNING "Исключённого файла ${excluded} больше нет в списке OrcaSlicer: проверьте исключение.")
    endif ()
    list(REMOVE_ITEM orca_kept "${excluded}")
endforeach ()
list(TRANSFORM orca_kept PREPEND "${LIBSLIC3R_DIR}/")
foreach (entry IN LISTS ORCA_LIBSLIC3R_PATCHED)
    string(REPLACE "=" ";" entry "${entry}")
    list(GET entry 0 rel_path)
    list(GET entry 1 copy)
    list(FIND orca_kept "${LIBSLIC3R_DIR}/${rel_path}" index)
    if (index EQUAL -1)
        message(FATAL_ERROR "Исправленного файла ${rel_path} нет среди компилируемых.")
    endif ()
    list(REMOVE_AT orca_kept ${index})
    list(INSERT orca_kept ${index} "${copy}")
endforeach ()
list(LENGTH orca_all n_all)
list(LENGTH orca_kept n_kept)
message(STATUS "libslic3r: ${n_kept} из ${n_all} исходников OrcaSlicer")

# FlushVolCalc.cpp зовёт RGB2HSV() из src/slic3r/Utils/ColorSpaceConvert.cpp,
# то есть из библиотеки окна программы. Файл берётся как есть, вместо wx
# ему подставляется маленький wxColour (shims/wx).
set(ORCA_GUI_SOURCES "${ORCA_SRC}/src/slic3r/Utils/ColorSpaceConvert.cpp")
set_source_files_properties(${ORCA_GUI_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${REPO_DIR}/shims/wx"
    SKIP_PRECOMPILE_HEADERS ON)

set(ENGINE_STUBS
    "${REPO_DIR}/stubs/Format/STEP.cpp"
    "${REPO_DIR}/stubs/Format/svg.cpp"
    "${REPO_DIR}/stubs/Format/DRC.cpp"
    "${REPO_DIR}/stubs/Format/AssimpImport.cpp"
    "${REPO_DIR}/stubs/pthread_name.c"
    "${LIBSLIC3R_GEN_DIR}/SLA/DrainHole.cpp")

# Заголовки-подмены: openssl/md5.h (Utils.hpp, его подключают почти все)
# и OCCT для Format/STEP.hpp (его подключает Model.hpp). Публично: мост тоже
# подключает Model.hpp.
add_library(engine_shims INTERFACE)
target_include_directories(engine_shims SYSTEM INTERFACE "${REPO_DIR}/shims/openssl" "${REPO_DIR}/shims/occt")

# ---- libslic3r_cgal ---------------------------------------------------------------
add_library(libslic3r_cgal STATIC
    "${LIBSLIC3R_DIR}/CutSurface.cpp"
    "${LIBSLIC3R_DIR}/IntersectionPoints.cpp"
    "${LIBSLIC3R_DIR}/MeshBoolean.cpp"
    "${LIBSLIC3R_DIR}/MeshRepair.cpp"
    "${LIBSLIC3R_DIR}/TryCatchSignal.cpp"
    "${LIBSLIC3R_DIR}/Triangulation.cpp")
target_include_directories(libslic3r_cgal PRIVATE "${LIBSLIC3R_GEN_DIR}")

# Как у OrcaSlicer: -frounding-math от CGAL только для libslic3r_cgal.
get_target_property(cgal_target CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${cgal_target})
    set(cgal_target CGAL::CGAL)
endif ()
get_target_property(cgal_options ${cgal_target} INTERFACE_COMPILE_OPTIONS)
if (cgal_options)
    set(rounding "${cgal_options}")
    set(others "${cgal_options}")
    list(FILTER rounding INCLUDE REGEX frounding-math)
    list(FILTER others EXCLUDE REGEX frounding-math)
    set_target_properties(${cgal_target} PROPERTIES INTERFACE_COMPILE_OPTIONS "${others}")
    target_compile_options(libslic3r_cgal PRIVATE ${rounding})
endif ()
# CGAL_LINKED_WITH_TBB, как у OrcaSlicer, здесь не задаётся: потоков нет,
# и CGAL сам выбирает последовательные алгоритмы.
target_link_libraries(libslic3r_cgal PRIVATE ${cgal_target} admesh libigl mcut boost_libs TBB::tbb engine_shims)

# ---- libslic3r ----------------------------------------------------------------------
add_library(libslic3r STATIC ${orca_kept} ${ORCA_GUI_SOURCES} ${ENGINE_STUBS})
target_compile_definitions(libslic3r PUBLIC USE_TBB TBB_USE_CAPTURED_EXCEPTION=0)
target_compile_features(libslic3r PUBLIC cxx_std_17)
target_include_directories(libslic3r
    PRIVATE "${LIBSLIC3R_DIR}" "${LIBSLIC3R_DIR}/TextureToColor"
    PUBLIC "${LIBSLIC3R_GEN_DIR}")
target_include_directories(libslic3r SYSTEM PUBLIC ${EXPAT_INCLUDE_DIRS} "${ORCA_SRC}/src")
# Подмена hw_rounding.hpp из Boost должна стоять в поиске раньше префикса.
target_include_directories(libslic3r SYSTEM BEFORE PRIVATE "${REPO_DIR}/shims/boost-interval")

# Список OrcaSlicer без библиотек рабочего стола (OpenCV, Assimp, OCCT,
# Draco, OpenSSL, FreeType, fontconfig, dl).
target_link_libraries(libslic3r
    PUBLIC
        Eigen3::Eigen
        admesh
        libigl
        libnest2d
        miniz
        engine_shims
    PRIVATE
        ${EXPAT_LIBRARIES}
        boost_libs
        cereal::cereal
        Clipper2
        glu-libtess
        JPEG::JPEG
        libslic3r_cgal
        mcut
        noise::noise
        PNG::PNG
        qhull
        qoi
        semver
        TBB::tbb
        ZLIB::ZLIB)

# Как у OrcaSlicer: pchheader.hpp предкомпилирован и подключён в каждый файл.
# Те же единицы трансляции, и сборка заметно быстрее.
if (ENGINE_PCH)
    target_precompile_headers(libslic3r PRIVATE "${LIBSLIC3R_DIR}/pchheader.hpp")
    set_source_files_properties("${REPO_DIR}/stubs/pthread_name.c" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif ()
