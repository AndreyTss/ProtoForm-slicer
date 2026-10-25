# Какие исходники libslic3r компилируются и какие из них правятся при сборке.
#
# Каталог orca/ не меняется никогда. Список файлов читается из CMakeLists
# самого OrcaSlicer, чтобы не держать его копию; из списка убираются файлы,
# которым нужны библиотеки рабочего стола (OCCT, OpenCV, OpenVDB, Assimp,
# Draco). Нужные правки делаются в копиях файлов в дереве сборки.

# orca_libslic3r_sources(<переменная>): .cpp из блока set(lisbslic3r_sources ...)
# в orca/src/libslic3r/CMakeLists.txt, пути от src/libslic3r. Блоки под
# SLIC3R_CAD и APPLE не берутся: в этой сборке они выключены.
function(orca_libslic3r_sources out_var)
    set(cmake_file "${ORCA_SRC}/src/libslic3r/CMakeLists.txt")
    file(READ "${cmake_file}" text)
    # Сначала комментарии: в списке есть закомментированные файлы.
    string(REGEX REPLACE "#[^\n]*" "" text "${text}")
    string(REGEX MATCH "set\\(lisbslic3r_sources([^)]*)\\)" block "${text}")
    if (NOT block)
        message(FATAL_ERROR "В ${cmake_file} не найден set(lisbslic3r_sources ...): поменялся CMake OrcaSlicer.")
    endif ()
    string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.cpp" sources "${CMAKE_MATCH_1}")
    list(REMOVE_DUPLICATES sources)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cmake_file}")
    set(${out_var} "${sources}" PARENT_SCOPE)
endfunction()

# Не компилируются. Те, на которые ссылается оставшийся код, заменены
# заглушками из stubs/ с тем же путём.
set(ORCA_LIBSLIC3R_EXCLUDED
    Format/STEP.cpp                    # OCCT; заглушка
    Format/svg.cpp                     # OCCT; заглушка
    Format/AssimpImport.cpp            # Assimp; заглушка
    Format/DRC.cpp                     # Draco; заглушка
    Shape/TextShape.cpp                # OCCT, только для окна программы
    ObjColorUtils.cpp                  # OpenCV
    TexturePainting.cpp                # OpenCV
    TextureToColor/TextureToColor.cpp  # OpenCV
    TextureToColor/ColorUtils.cpp      # OpenCV
    SLA/Hollowing.cpp                  # OpenVDB; методы DrainHole копируются, см. ниже
    SLAPrint.cpp                       # печать смолой, нарезке FDM не нужна
    SLAPrintSteps.cpp
    Format/SL1.cpp
    pchheader.cpp                      # заголовок подключается как предкомпилированный
)

# orca_patch_source(<путь от src/libslic3r> <переменная>
#                   FIND <текст> REPLACE <текст> [FIND ... REPLACE ...]):
# пишет копию файла с заменами в дерево сборки и возвращает её путь.
# Каждый FIND обязан встретиться ровно один раз: если OrcaSlicer поменял
# код, настройка падает, а не собирает молча не то.
function(orca_patch_source rel_path out_var)
    set(orca_file "${ORCA_SRC}/src/libslic3r/${rel_path}")
    set(out_file "${CMAKE_BINARY_DIR}/orca-patched/${rel_path}")
    file(READ "${orca_file}" text)

    # Пары читаются через ARGV<n>: в текстах бывает «;», и ARGN их разрезал бы.
    math(EXPR pairs "(${ARGC} - 2) / 4")
    math(EXPR rest "(${ARGC} - 2) % 4")
    if (pairs EQUAL 0 OR NOT rest EQUAL 0)
        message(FATAL_ERROR "orca_patch_source(${rel_path}): нужны пары FIND <текст> REPLACE <текст>.")
    endif ()
    math(EXPR last "${pairs} - 1")
    foreach (pair RANGE ${last})
        math(EXPR i_find "3 + 4 * ${pair}")
        math(EXPR i_replace "5 + 4 * ${pair}")
        set(find "${ARGV${i_find}}")
        set(replace "${ARGV${i_replace}}")
        string(FIND "${text}" "${find}" first)
        string(FIND "${text}" "${find}" second REVERSE)
        if (first EQUAL -1 OR NOT first EQUAL second)
            message(FATAL_ERROR "Замена в ${rel_path} не подходит: текст\n  ${find}\nдолжен встречаться ровно один раз.")
        endif ()
        string(REPLACE "${find}" "${replace}" text "${text}")
    endforeach ()

    # #line: ошибки и __FILE__ указывают на исходный файл, а не на копию.
    file(WRITE "${out_file}.tmp" "#line 1 \"${orca_file}\"\n${text}")
    # Перезаписывать только при изменении, иначе каждая настройка пересобирает файл.
    file(COPY_FILE "${out_file}.tmp" "${out_file}" ONLY_IF_DIFFERENT)
    file(REMOVE "${out_file}.tmp")
    # Кавычечные #include копии должны находиться рядом с оригиналом.
    get_filename_component(orca_dir "${orca_file}" DIRECTORY)
    set_source_files_properties("${out_file}" PROPERTIES INCLUDE_DIRECTORIES "${orca_dir}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${orca_file}")
    set(${out_var} "${out_file}" PARENT_SCOPE)
endfunction()

# orca_drainhole_source(<файл>): из SLA/Hollowing.cpp нужны только методы
# sla::DrainHole (их зовут AABBMesh, IndexedMesh и чтение 3MF), остальное
# требует OpenVDB. Методы вырезаются из файла OrcaSlicer как есть, от
# DrainHole::to_mesh до cut_drainholes, и оборачиваются нужными #include.
function(orca_drainhole_source out_file)
    set(orca_file "${ORCA_SRC}/src/libslic3r/SLA/Hollowing.cpp")
    file(READ "${orca_file}" text)
    string(FIND "${text}" "indexed_triangle_set DrainHole::to_mesh() const" begin)
    string(FIND "${text}" "void cut_drainholes(" end)
    if (begin EQUAL -1 OR end EQUAL -1 OR NOT end GREATER begin)
        message(FATAL_ERROR "В ${orca_file} не найдены методы DrainHole: поменялся Hollowing.cpp.")
    endif ()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${text}" ${begin} ${length} methods)
    foreach (name DrainHole::to_mesh DrainHole::operator== DrainHole::is_inside DrainHole::get_intersections)
        string(FIND "${methods}" "${name}" pos)
        if (pos EQUAL -1)
            message(FATAL_ERROR "${name} больше не лежит между метками в ${orca_file}.")
        endif ()
    endforeach ()
    file(WRITE "${out_file}.tmp"
        "// Методы sla::DrainHole из ${orca_file}, вырезаны при настройке сборки.\n"
        "#include <libslic3r/libslic3r.h>\n"
        "#include <libslic3r/TriangleMesh.hpp>\n"
        "#include <libslic3r/SLA/Hollowing.hpp>\n"
        "#include <libslic3r/SLA/IndexedMesh.hpp>\n"
        "#include <libslic3r/SLA/SupportTreeMesher.hpp>\n"
        "namespace Slic3r { namespace sla {\n"
        "${methods}\n"
        "}} // namespace Slic3r::sla\n")
    file(COPY_FILE "${out_file}.tmp" "${out_file}" ONLY_IF_DIFFERENT)
    file(REMOVE "${out_file}.tmp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${orca_file}")
endfunction()

# ---- Замены в исходниках OrcaSlicer -------------------------------------------
# Каждая: файл, что меняется и почему. ORCA_LIBSLIC3R_PATCHED: «оригинал=копия».
set(ORCA_LIBSLIC3R_PATCHED)

# Platform.cpp: для неизвестной платформы стоит static_assert(false), и clang
# его не пропускает, хотя ветка рядом умеет отвечать «Unknown». Убираем
# только эту строку: Emscripten и есть неизвестная платформа.
orca_patch_source(Platform.cpp patched
    FIND    "static_assert(false, \"Unknown platform detected\");"
    REPLACE "/* wasm: static_assert removed, Unknown is the right answer here */")
list(APPEND ORCA_LIBSLIC3R_PATCHED "Platform.cpp=${patched}")

# Arachne/SkeletalTrapezoidation.cpp: цикл начинается с size() - 1, присвоенного
# coord_t (int64). Для пустого вектора на 64-битной машине это -1, а в wasm32
# size_t 32-битный, и выходит 4294967295: цикл читает за границей массива.
# Приведение до вычитания даёт -1 везде, на 64 битах поведение то же.
orca_patch_source(Arachne/SkeletalTrapezoidation.cpp patched
    FIND    "for (next_inset_idx = left.toolpath_locations.size() - 1; next_inset_idx >= 0; next_inset_idx--)"
    REPLACE "for (next_inset_idx = coord_t(left.toolpath_locations.size()) - 1; next_inset_idx >= 0; next_inset_idx--)")
list(APPEND ORCA_LIBSLIC3R_PATCHED "Arachne/SkeletalTrapezoidation.cpp=${patched}")
