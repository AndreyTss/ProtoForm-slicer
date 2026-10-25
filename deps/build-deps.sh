#!/usr/bin/env bash
# Собирает зависимости libslic3r в WebAssembly (Emscripten 6.0.10, один поток)
# и ставит их в общий префикс. Запускается внутри контейнера сборки после
# deps/fetch-deps.sh.
#
#   bash deps/build-deps.sh               все шаги
#   bash deps/build-deps.sh boost cgal    только названные
#
# Шаги: ports boost eigen cereal cgal nlopt qhull libnoise jpeg toolchain check
#
# Пути (WORK по умолчанию /work, это том Docker):
#   $WORK/deps-src/       исходники (fetch-deps.sh)
#   $WORK/deps-build/     деревья сборки, по папке на пакет
#   $WORK/prefix/         установленные библиотеки и заголовки
#   $WORK/prefix/share/slicer-deps/toolchain.cmake
#                         настройки для сборки движка: флаги и пути поиска
#
# Каждый шаг помечается отметкой с хешем своего текста и общих флагов, так что
# повторный запуск пересобирает только то, что поменялось. Чтобы пересобрать
# всё, удалите $WORK/prefix/.stamps.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${WORK:-/work}
SRC=$WORK/deps-src
BUILD=$WORK/deps-build
PREFIX=$WORK/prefix
STAMPS=$PREFIX/.stamps
LOG=$PREFIX/share/slicer-deps/timings.log
JOBS=${JOBS:-$(nproc)}
[[ -n $JOBS ]] || JOBS=$(nproc)
EM_CACHE=${EM_CACHE:?нужен EM_CACHE: он задан в образе сборки}

EMCC_VERSION=$(emcc --version | head -1)
case $EMCC_VERSION in
  *" 6.0.10 "*) ;;
  *) echo "Нужен Emscripten 6.0.10, а найден: $EMCC_VERSION" >&2; exit 1 ;;
esac

# ---- Флаги -----------------------------------------------------------------
# Одинаковые для всех зависимостей и для самого движка: объекты с разной
# моделью исключений или longjmp не компонуются вместе.
#
# * Исключения C++ — родные исключения WebAssembly (-fwasm-exceptions): они
#   быстрее и меньше, чем исключения через JavaScript. Кодировка «legacy»
#   нужна старым Safari на iPhone и iPad; новая (exnref) их отрезала бы.
# * longjmp тоже на исключениях WebAssembly. Для файлов на C, которые
#   компилируются без -fwasm-exceptions, это нужно сказать явно, иначе
#   Emscripten выберет longjmp через JavaScript и объекты не скомпонуются
#   (qhull, libpng и libjpeg пользуются setjmp).
# * Потоков нет (без -pthread). Тогда musl объявляет, что потоков нет вовсе,
#   Boost.Config убирает BOOST_HAS_THREADS, и <boost/thread.hpp> отказывается
#   компилироваться. Boost.Thread и Boost.Log нужны libslic3r даже в одном
#   потоке, поэтому BOOST_HAS_PTHREADS задаётся вручную: они собираются против
#   заглушек pthread из Emscripten. BOOST_LOG_NO_THREADS не годится:
#   utils.cpp из OrcaSlicer подключает async_frontend.hpp, а тот с этим
#   макросом не компилируется.
# * Без -msimd128 и без -flto: арифметика Eigen и CGAL остаётся скалярной,
#   как в обычной сборке OrcaSlicer под x86-64.
# * -ffile-prefix-map: пути тома в строках __FILE__ (они попадают в wasm через
#   исключения Boost) записываются как /work и /emcache, так что результат не
#   зависит от того, где лежит том.
SJLJ="-sSUPPORT_LONGJMP=wasm -sWASM_LEGACY_EXCEPTIONS=1"
MAPS="-ffile-prefix-map=$WORK=/work -ffile-prefix-map=$EM_CACHE=/emcache"
CFLAGS_ALL="$SJLJ $MAPS"
CXXFLAGS_ALL="-fwasm-exceptions $SJLJ -DBOOST_HAS_PTHREADS $MAPS"
LDFLAGS_ALL="-fwasm-exceptions $SJLJ"

# Эти два определения нужны только коду, который подключает CGAL (сама CGAL
# ставится одними заголовками), поэтому они уходят в настройки движка:
# * CGAL_ALWAYS_ROUND_TO_NEAREST: в WebAssembly нельзя переключить режим
#   округления процессора (fesetround ничего не делает), и интервальные
#   фильтры CGAL без этого макроса молча ошибались бы. С ним CGAL строит
#   интервалы через nextafter.
# * CGAL_DISABLE_GMP: точные числа CGAL берёт из Boost.Multiprecision, а не из
#   GMP и MPFR. При нарезке CGAL нужна только для точных предикатов (проверки
#   диаграммы Вороного у стенок Arachne), их ответ не зависит от того, чем
#   считаются точные числа. Сборка GMP в WebAssembly лишь добавила бы мегабайты.
ENGINE_DEFINES="-DCGAL_ALWAYS_ROUND_TO_NEAREST -DCGAL_DISABLE_GMP"

CMAKE_COMMON=(
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  # С типом PATH: libjpeg-turbo объявляет эту переменную сам и иначе превратил
  # бы «lib» в путь от текущей папки.
  -DCMAKE_INSTALL_LIBDIR:PATH=lib
  # Emscripten.cmake ищет пакеты только внутри CMAKE_FIND_ROOT_PATH (и своего
  # sysroot), поэтому префикс надо указать именно здесь. Заодно ничего
  # не подхватится из самой системы контейнера.
  -DCMAKE_FIND_ROOT_PATH="$PREFIX"
  -DCMAKE_PREFIX_PATH="$PREFIX"
  -DCMAKE_C_FLAGS="$CFLAGS_ALL"
  -DCMAKE_CXX_FLAGS="$CXXFLAGS_ALL"
  -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS_ALL"
  -DBUILD_SHARED_LIBS=OFF
  # Старые проекты (nlopt, qhull) просят CMake 2.x; без этого CMake 3.31
  # предупреждает, а 4.x отказывается.
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

FLAGS_HASH=$(printf '%s|' "$EMCC_VERSION" "$CFLAGS_ALL" "$CXXFLAGS_ALL" "$LDFLAGS_ALL" "${CMAKE_COMMON[*]}" \
  | sha256sum | cut -c1-12)
mkdir -p "$STAMPS" "$BUILD" "$(dirname "$LOG")"

STEP_HASH=""
stamp()      { echo "$STAMPS/$1-$FLAGS_HASH-$STEP_HASH"; }
is_done()    { [[ -f $(stamp "$1") ]]; }
mark_done()  { rm -f "$STAMPS/$1-"*; touch "$(stamp "$1")"; }

# cmake_package <имя> <исходники> [--target <цель>] [-D...]: настроить,
# собрать и установить один проект CMake.
cmake_package() {
  local name=$1 src=$2; shift 2
  local target=()
  if [[ ${1:-} == --target ]]; then target=(--target "$2"); shift 2; fi
  if is_done "$name"; then echo "[есть] $name"; return; fi
  echo "==== $name"
  rm -rf "${BUILD:?}/$name"
  emcmake cmake -S "$src" -B "$BUILD/$name" "${CMAKE_COMMON[@]}" "$@"
  cmake --build "$BUILD/$name" -j "$JOBS" "${target[@]}"
  cmake --install "$BUILD/$name"
  mark_done "$name"
}

# ---- Шаги ------------------------------------------------------------------

# zlib и libpng — готовые порты Emscripten (версии и суммы закреплены в самом
# emsdk). Вариант libpng с «legacysjlj» соответствует нашей модели longjmp.
# OrcaSlicer просит zlib 1.2.13 и libpng 1.6.35, но при нарезке они нужны
# только для чтения и записи PNG, на результат нарезки это не влияет.
step_ports() {
  if is_done ports && [[ -f $EM_CACHE/sysroot/lib/wasm32-emscripten/libz.a ]]; then
    echo "[есть] ports"; return
  fi
  embuilder build zlib libpng-legacysjlj
  mark_done ports
}

# Boost 1.84.0 своей сборкой CMake, как в orca/deps. Ставятся все заголовки
# (libslic3r и CGAL пользуются десятками библиотек из одних заголовков)
# и все компилируемые части, кроме тех, что в браузере не собираются
# или заведомо не нужны.
# * context: Boost.Log тянет asio, asio тянет context, поэтому выключить его
#   нельзя. Его ассемблерная реализация рассчитана на x86, а реализация
#   на ucontext компилируется. libslic3r её не вызывает, так что до
#   компоновки она не доходит.
# * locale: без ICU, как в OrcaSlicer. Бэкенд POSIX Boost включает только для
#   Linux и macOS; включаем вручную, чтобы поведение совпало со сборкой
#   OrcaSlicer под Linux.
# * iostreams без сжатия: libslic3r нужны только массивы и mapped_file.
step_boost() {
  cmake_package boost "$SRC/boost-1.84.0" \
    -DBOOST_EXCLUDE_LIBRARIES="cobalt;contract;coroutine2;fiber;graph_parallel;mpi;numpy;property_map_parallel;python;stacktrace;test;wave" \
    -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF \
    -DBOOST_INSTALL_LAYOUT=system \
    -DBOOST_CONTEXT_IMPLEMENTATION=ucontext \
    -DBOOST_LOCALE_ENABLE_ICU=OFF -DBOOST_LOCALE_ENABLE_POSIX=ON \
    -DBOOST_IOSTREAMS_ENABLE_ZLIB=OFF -DBOOST_IOSTREAMS_ENABLE_BZIP2=OFF \
    -DBOOST_IOSTREAMS_ENABLE_LZMA=OFF -DBOOST_IOSTREAMS_ENABLE_ZSTD=OFF \
    -DBOOST_LOG_WITHOUT_SYSLOG=ON -DBOOST_LOG_WITHOUT_IPC=ON
}

# Библиотеки из одних заголовков: ставятся ради пакетных файлов CMake,
# по которым их находит сборка libslic3r.
step_eigen() {
  cmake_package eigen "$SRC/eigen-5.0.1" \
    -DBUILD_TESTING=OFF -DEIGEN_BUILD_TESTING=OFF -DEIGEN_BUILD_DOC=OFF -DEIGEN_BUILD_DEMOS=OFF \
    -DEIGEN_BUILD_BLAS=OFF -DEIGEN_BUILD_LAPACK=OFF -DEIGEN_BUILD_PKGCONFIG=OFF
}
step_cereal() {
  cmake_package cereal "$SRC/cereal-1.3.0" \
    -DJUST_INSTALL_CEREAL=ON -DSKIP_PERFORMANCE_COMPARISON=ON -DBUILD_TESTS=OFF -DBUILD_SANDBOX=OFF -DBUILD_DOC=OFF
}
step_cgal() {
  cmake_package cgal "$SRC/cgal-6.2.1"
}

# Небольшие компилируемые библиотеки.
step_nlopt() {
  # Только сама библиотека: nlopt 2.5.0 всегда добавляет свои тесты.
  cmake_package nlopt "$SRC/nlopt-2.5.0" --target nlopt \
    -DNLOPT_CXX=OFF -DNLOPT_PYTHON=OFF -DNLOPT_OCTAVE=OFF -DNLOPT_MATLAB=OFF -DNLOPT_GUILE=OFF \
    -DNLOPT_SWIG=OFF -DNLOPT_LINK_PYTHON=OFF
}
step_qhull() {
  cmake_package qhull "$SRC/qhull-8.0.2" \
    -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DLINK_APPS_SHARED=OFF -DINCLUDE_INSTALL_DIR=include
}
step_libnoise() {
  cmake_package libnoise "$SRC/libnoise-1.0"
}

# libjpeg-turbo, а не порт libjpeg из Emscripten: GCode/Thumbnails.cpp пишет
# миниатюры с цветовым пространством JCS_EXT_RGBA, которого в обычном libjpeg
# нет. SIMD у libjpeg-turbo только под x86 и ARM, выключаем. BUILD — строка
# сборки в сообщении о версии; по умолчанию это дата сборки, и тогда два
# одинаковых префикса, собранных в разные дни, различались бы.
step_jpeg() {
  cmake_package libjpeg-turbo "$SRC/libjpeg-turbo-3.2.0" \
    -DBUILD=orca-pin -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_JPEG8=ON \
    -DWITH_SIMD=OFF -DREQUIRE_SIMD=OFF -DWITH_TURBOJPEG=OFF -DWITH_JAVA=OFF -DWITH_FUZZ=OFF
}

# Настройки для сборки движка: те же флаги, пути поиска пакетов и подсказки
# FindZLIB и FindPNG, чтобы find_package из CMake OrcaSlicer нашёл порты.
step_toolchain() {
  local lib=$EM_CACHE/sysroot/lib/wasm32-emscripten
  local inc=$EM_CACHE/sysroot/include
  local f
  for f in "$lib/libz.a" "$lib/libpng-legacysjlj.a" "$inc/zlib.h" "$inc/png.h"; do
    [[ -f $f ]] || { echo "Нет $f: сначала шаг ports" >&2; exit 1; }
  done
  cat > "$PREFIX/share/slicer-deps/toolchain.cmake" <<EOF
# Создан deps/build-deps.sh, руками не править: перезапустите скрипт.
# Подключается к сборке движка через  emcmake cmake -C <этот файл> ...
#
# Флаги ниже — те, с которыми собраны все зависимости в префиксе. Движок
# обязан компилировать и компоновать каждый файл с ними же.
set(SLICER_DEPS_PREFIX "$PREFIX" CACHE PATH "Префикс зависимостей в WebAssembly")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(CMAKE_C_FLAGS "$CFLAGS_ALL" CACHE STRING "")
set(CMAKE_CXX_FLAGS "$CXXFLAGS_ALL $ENGINE_DEFINES" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS "$LDFLAGS_ALL" CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH "$PREFIX" CACHE STRING "")
set(CMAKE_PREFIX_PATH "$PREFIX" CACHE STRING "")
# find_package(Boost) через BoostConfig.cmake, а не через старый FindBoost.
set(CMAKE_POLICY_DEFAULT_CMP0167 NEW CACHE STRING "")
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "")
set(CGAL_DISABLE_GMP ON CACHE BOOL "")
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "")

# zlib и libpng — порты Emscripten в его кэше.
set(ZLIB_INCLUDE_DIR "$inc" CACHE PATH "")
set(ZLIB_LIBRARY "$lib/libz.a" CACHE FILEPATH "")
set(PNG_PNG_INCLUDE_DIR "$inc" CACHE PATH "")
set(PNG_LIBRARY "$lib/libpng-legacysjlj.a" CACHE FILEPATH "")
EOF
  {
    echo "emcc: $EMCC_VERSION"
    echo "CFLAGS: $CFLAGS_ALL"
    echo "CXXFLAGS: $CXXFLAGS_ALL"
    echo "LDFLAGS: $LDFLAGS_ALL"
    echo "для движка: $ENGINE_DEFINES"
    echo "--- архивы"
    cat "$HERE/SHA256SUMS"
  } > "$PREFIX/share/slicer-deps/manifest.txt"
  echo "записан $PREFIX/share/slicer-deps/toolchain.cmake"
}

# Проверка, что в префиксе есть всё, что ищет сборка libslic3r.
step_check() {
  local missing=0 f
  for f in \
    lib/cmake/Boost-1.84.0/BoostConfig.cmake \
    lib/libboost_filesystem.a lib/libboost_thread.a lib/libboost_log.a lib/libboost_log_setup.a \
    lib/libboost_locale.a lib/libboost_nowide.a lib/libboost_iostreams.a \
    lib/libboost_program_options.a lib/libboost_date_time.a lib/libboost_chrono.a lib/libboost_atomic.a \
    include/boost/version.hpp include/boost/multiprecision/cpp_int.hpp include/boost/polygon/voronoi.hpp \
    share/eigen3/cmake/Eigen3Config.cmake include/eigen3/Eigen/Core \
    include/cereal/cereal.hpp \
    lib/cmake/CGAL/CGALConfig.cmake include/CGAL/version.h \
    lib/libnlopt.a include/nlopt.h \
    lib/libqhullcpp.a lib/libqhullstatic_r.a lib/cmake/Qhull/QhullConfig.cmake \
    lib/liblibnoise_static.a include/libnoise/noise.h \
    lib/libjpeg.a include/jpeglib.h \
    share/slicer-deps/toolchain.cmake
  do
    [[ -e $PREFIX/$f ]] || { echo "НЕТ $f"; missing=1; }
  done
  [[ $missing == 0 ]] || { echo "Префикс неполный" >&2; exit 1; }
  echo "Префикс полный: $PREFIX"
}

ALL=(ports boost eigen cereal cgal nlopt qhull libnoise jpeg toolchain check)
STEPS=("$@"); [[ ${#STEPS[@]} -gt 0 ]] || STEPS=("${ALL[@]}")
for s in "${STEPS[@]}"; do
  declare -F "step_$s" >/dev/null || { echo "Нет шага '$s'. Шаги: ${ALL[*]}" >&2; exit 2; }
done
cd "$BUILD"   # всё, что сборки кладут в текущую папку, остаётся в деревьях сборки
for s in "${STEPS[@]}"; do
  # Отметка зависит от текста шага: правка рецепта пересобирает только его.
  STEP_HASH=$(declare -f "step_$s" | sha256sum | cut -c1-8)
  t0=$SECONDS
  "step_$s"
  printf '%s  %-10s %5d с  JOBS=%s\n' "$(date -u +%FT%TZ)" "$s" $((SECONDS - t0)) "$JOBS" >> "$LOG"
done
echo "build-deps: готово ($PREFIX)"
