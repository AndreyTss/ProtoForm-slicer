#!/usr/bin/env bash
# Скачивает исходники зависимостей libslic3r, сверяет контрольные суммы
# и распаковывает их. Запускается внутри контейнера сборки (docker/run.sh).
#
#   bash deps/fetch-deps.sh              все пакеты
#   bash deps/fetch-deps.sh boost cgal   только названные
#
# Куда (WORK по умолчанию /work, это том Docker):
#   $WORK/deps-src/_archives/<архив>     скачанные архивы
#   $WORK/deps-src/<имя>/                распакованные исходники
#
# Версии и адреса те же, что в orca/deps/*/*.cmake: движок должен собираться
# из того же, из чего собирается OrcaSlicer. Суммы лежат в deps/SHA256SUMS,
# и каждая из них обязана встречаться в orca/deps: так при смене версии
# в OrcaSlicer скрипт остановится, а не соберёт молча старое.
#
# Скачивается только то, чего нет; повторный запуск лишь сверяет суммы.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
WORK=${WORK:-/work}
SRC=$WORK/deps-src
ARCHIVES=$SRC/_archives
SUMS=$HERE/SHA256SUMS
ORCA_DEPS=$REPO/orca/deps

# имя | версия | адрес | файл в orca/deps, откуда взята версия
PACKAGES=(
  "boost|1.84.0|https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz|Boost/Boost.cmake"
  "eigen|5.0.1|https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.zip|Eigen/Eigen.cmake"
  "cereal|1.3.0|https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.0.zip|Cereal/Cereal.cmake"
  "cgal|6.2.1|https://github.com/CGAL/cgal/releases/download/v6.2.1/CGAL-6.2.1.zip|CGAL/CGAL.cmake"
  "nlopt|2.5.0|https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz|NLopt/NLopt.cmake"
  "qhull|8.0.2|https://github.com/qhull/qhull/archive/v8.0.2.zip|Qhull/Qhull.cmake"
  "libnoise|1.0|https://github.com/SoftFever/Orca-deps-libnoise/archive/refs/tags/1.0.zip|libnoise/libnoise.cmake"
  "libjpeg-turbo|3.2.0|https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.2.0.zip|JPEG/JPEG.cmake"
)

mkdir -p "$ARCHIVES"
wanted=" $* "

for entry in "${PACKAGES[@]}"; do
  IFS='|' read -r name version url pin <<<"$entry"
  if [[ $# -gt 0 && $wanted != *" $name "* ]]; then continue; fi

  # Архивы меток GitHub называются просто v1.3.0.zip, поэтому имя пакета
  # добавляется в начало: иначе два пакета могли бы занять один файл.
  file=$(basename "$url")
  case $file in v[0-9]*|[0-9]*) file=$name-$file ;; esac
  archive=$ARCHIVES/$file
  dest=$SRC/$name-$version

  expected=$(awk -v f="$file" '$2 == f { print $1 }' "$SUMS")
  if [[ -z $expected ]]; then
    echo "[ошибка] нет суммы для $file в $SUMS" >&2; exit 1
  fi
  if [[ -f $ORCA_DEPS/$pin ]] && ! grep -qi "$expected" "$ORCA_DEPS/$pin"; then
    echo "[ошибка] сумма $file не совпадает с orca/deps/$pin: OrcaSlicer сменил версию?" >&2; exit 1
  fi

  if [[ ! -s $archive ]]; then
    echo "[скачать] $name $version <- $url"
    curl -fsSL --retry 3 --retry-delay 5 -o "$archive.part" "$url"
    mv "$archive.part" "$archive"
  fi
  actual=$(sha256sum "$archive" | cut -d' ' -f1)
  if [[ $actual != "$expected" ]]; then
    echo "[ошибка] $file: sha256 $actual, ожидалась $expected" >&2
    rm -f "$archive"
    exit 1
  fi

  # Метка .extracted хранит сумму архива: если архив сменился, распаковать заново.
  if [[ ! -f $dest/.extracted ]] || [[ $(cat "$dest/.extracted") != "$actual" ]]; then
    rm -rf "$dest"; mkdir -p "$dest"
    bsdtar -xf "$archive" -C "$dest" --strip-components=1
    echo "$actual" > "$dest/.extracted"
    echo "[готово] $name $version -> $dest"
  else
    echo "[есть]   $name $version"
  fi
done
echo "fetch-deps: всё на месте ($SRC)"
