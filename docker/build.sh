#!/usr/bin/env bash
# Сборка с нуля одной командой (с Windows в Git Bash или из Linux):
#
#   bash docker/build.sh                 все шаги
#   bash docker/build.sh engine test     только названные
#
# Шаги:
#   image   образ сборки (docker/Dockerfile)
#   sync    исходники в том (docker/sync.sh)
#   deps    скачать и собрать зависимости (deps/*.sh)
#   engine  libslic3r, smoke и, если есть мост, engine.mjs + engine.wasm
#   test    smoke.js в Node: кубик режется, печатается число слоёв
#   dist    готовые engine.mjs, engine.wasm и smoke.* в dist/ репозитория
#
# JOBS — число параллельных задач компиляции (по умолчанию все ядра Docker).
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
IMAGE=${IMAGE:-protoform-slicer-build}
WORK_VOLUME=${WORK_VOLUME:-protoform-slicer-work}
export IMAGE WORK_VOLUME

in_container() { bash "$HERE/run.sh" "set -euo pipefail; $*"; }

step_image() { docker build -t "$IMAGE" "$HERE"; }
step_sync() { bash "$HERE/sync.sh"; }
step_deps() { in_container 'bash deps/fetch-deps.sh && bash deps/build-deps.sh'; }

step_engine() {
  in_container '
    B=/work/build-engine
    emcmake cmake -C /work/prefix/share/slicer-deps/toolchain.cmake -G Ninja -S engine -B $B \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    targets="libslic3r smoke"
    if cmake --build $B --target help | grep -q "^engine:"; then targets="$targets engine"; fi
    cmake --build $B -j ${JOBS:-$(nproc)} --target $targets
    ls -l $B/out $B/liblibslic3r.a $B/liblibslic3r_cgal.a'
}

step_test() { in_container 'cd /work/build-engine/out && node smoke.js'; }

step_dist() {
  mkdir -p "$REPO/dist"
  local host_dist=$REPO/dist
  if command -v cygpath >/dev/null 2>&1; then host_dist=$(cygpath -m "$REPO/dist"); fi
  MSYS_NO_PATHCONV=1 docker run --rm -v "$WORK_VOLUME:/work" -v "$host_dist:/dist" "$IMAGE" \
    bash -c 'cp -v /work/build-engine/out/* /dist/'
}

ALL=(image sync deps engine test)
STEPS=("$@"); [[ ${#STEPS[@]} -gt 0 ]] || STEPS=("${ALL[@]}")
for s in "${STEPS[@]}"; do
  declare -F "step_$s" >/dev/null || { echo "Нет шага '$s'. Шаги: image sync deps engine test dist" >&2; exit 2; }
done
for s in "${STEPS[@]}"; do
  echo "==== $s"
  t0=$SECONDS
  "step_$s"
  echo "[время] $s: $((SECONDS - t0)) с"
done
