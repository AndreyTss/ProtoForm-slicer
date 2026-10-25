#!/usr/bin/env bash
# Копирует исходники репозитория в рабочий том Docker (/work/src).
#
#   bash docker/sync.sh
#
# Копируются только изменённые файлы (rsync), лишнее в томе удаляется, так что
# после правки на Windows достаточно запустить скрипт ещё раз. Не копируются:
# папки git, результаты сборки и тяжёлые ресурсы OrcaSlicer, которые движку
# не нужны (картинки интерфейса, шрифты, справка, переводы). Профили
# принтеров и файлы resources/info, resources/flush остаются: их читает мост.
#
# Коммиты репозитория и OrcaSlicer записываются в /work/src/SOURCE_COMMITS:
# внутри тома нет папок git, а сборке нужно знать, из чего она собрана.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
IMAGE=${IMAGE:-protoform-slicer-build}
WORK_VOLUME=${WORK_VOLUME:-protoform-slicer-work}

# Docker на Windows понимает путь вида X:/папка, а не /x/папка.
host_path=$REPO
if command -v cygpath >/dev/null 2>&1; then host_path=$(cygpath -m "$REPO"); fi

engine_commit=$(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)
orca_commit=$(git -C "$REPO/orca" rev-parse HEAD 2>/dev/null || echo unknown)
# Незакоммиченные правки помечаются, чтобы сборку из них не приняли за чистую.
if [[ -n $(git -C "$REPO" status --porcelain --untracked-files=no 2>/dev/null) ]]; then
  engine_commit="$engine_commit-dirty"
fi
if [[ -n $(git -C "$REPO/orca" status --porcelain --untracked-files=no 2>/dev/null) ]]; then
  orca_commit="$orca_commit-dirty"
fi

# Только после вызовов git: git для Windows сам должен перевести /d/... в D:/...
export MSYS_NO_PATHCONV=1
docker volume create "$WORK_VOLUME" >/dev/null

docker run --rm -v "$host_path:/host:ro" -v "$WORK_VOLUME:/work" "$IMAGE" bash -c "
  set -euo pipefail
  mkdir -p /work/src
  rsync -a --delete --checksum-choice=xxh64 \
    --exclude='.git' \
    --exclude='/build*/' --exclude='/out/' --exclude='/dist/' \
    --exclude='/deps-src/' --exclude='/deps-build/' \
    --exclude='node_modules/' --exclude='/bench/models/' --exclude='/bench/results/' \
    --exclude='/orca/localization/' --exclude='/orca/doc/' --exclude='/orca/SoftFever_doc/' \
    --exclude='/orca/sandboxes/' --exclude='/orca/tests/' \
    --exclude='/orca/resources/hms/' --exclude='/orca/resources/images/' \
    --exclude='/orca/resources/fonts/' --exclude='/orca/resources/web/' \
    --exclude='/orca/resources/textures/' --exclude='/orca/resources/handy_models/' \
    --exclude='/orca/resources/calib/' \
    --exclude='/SOURCE_COMMITS' \
    /host/ /work/src/
  printf 'engine=%s\norca=%s\n' '$engine_commit' '$orca_commit' > /work/src/SOURCE_COMMITS
  echo \"sync: /work/src <- $host_path (engine $engine_commit, orca $orca_commit)\"
"
