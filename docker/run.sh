#!/usr/bin/env bash
# Запуск команды внутри контейнера сборки.
#
#   bash docker/run.sh <команда bash>        например: bash docker/run.sh 'emcc --version'
#   bash docker/run.sh                        оболочка внутри контейнера
#
# Рабочий том (WORK_VOLUME, по умолчанию protoform-slicer-work) подключается
# в /work: там копия исходников (/work/src), зависимости и деревья сборки.
# Диск Windows в контейнер не подключается: сборка на нём шла бы в разы
# медленнее. Исходники попадают в том через docker/sync.sh.
set -euo pipefail

IMAGE=${IMAGE:-protoform-slicer-build}
WORK_VOLUME=${WORK_VOLUME:-protoform-slicer-work}
JOBS=${JOBS:-}

# Git Bash превращает пути вида /work в C:/Program Files/Git/work. Для docker
# это лишнее: пути внутри контейнера должны остаться как есть.
export MSYS_NO_PATHCONV=1

# Скрипт окружения emsdk в образе сбрасывает EM_CACHE, поэтому кэш задаётся
# здесь. Первый запуск копирует в том готовый кэш из образа (системные
# библиотеки уже собраны), дальше кэш живёт на томе.
PRELUDE='export EM_CACHE=/work/emcache; [ -d /work/emcache ] || cp -a /emsdk/upstream/emscripten/cache /work/emcache;'

tty=()
if [[ -t 0 && -t 1 ]]; then tty=(-it); fi

if [[ $# -eq 0 ]]; then
  exec docker run --rm "${tty[@]}" -v "$WORK_VOLUME:/work" -e JOBS="$JOBS" "$IMAGE" bash -c "$PRELUDE exec bash"
fi
exec docker run --rm "${tty[@]}" -v "$WORK_VOLUME:/work" -e JOBS="$JOBS" "$IMAGE" bash -c "$PRELUDE $*"
