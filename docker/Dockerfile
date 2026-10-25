# Образ сборки движка: Linux x86-64 с Emscripten 6.0.10.
#
# Версия Emscripten закреплена: на ней OrcaSlicer 2.5 заведомо собирается
# в WebAssembly, а смена версии меняет обработку исключений и longjmp, и тогда
# все зависимости нужно пересобирать и проверять заново. Образ официальный,
# от авторов Emscripten.
#
# В образе только инструменты. Исходники, зависимости и деревья сборки живут
# в именованном томе Docker (см. docs/build.md): сборка на томе
# с диска Windows шла бы в разы медленнее из-за прослойки файловой системы.
#
#   docker build -t protoform-slicer-build docker
FROM emscripten/emsdk:6.0.10

# CMake 3.31: у Ubuntu в образе версия старее, а пакетные файлы Boost и CGAL
# рассчитаны на свежую.
# Архив с официальной страницы выпусков Kitware, сумма взята из их файла
# cmake-3.31.6-SHA-256.txt и сверяется при сборке образа.
ARG CMAKE_VERSION=3.31.6
ARG CMAKE_SHA256=5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl git libarchive-tools ninja-build ccache rsync xz-utils bzip2 \
 && rm -rf /var/lib/apt/lists/* \
 && curl -fsSL -o /tmp/cmake.tar.gz \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
 && echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum -c - \
 && mkdir -p /opt/cmake \
 && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1 \
 && rm /tmp/cmake.tar.gz

ENV PATH=/opt/cmake/bin:$PATH

# Всё рабочее лежит на томе /work. Кэш Emscripten (EM_CACHE) задаёт
# docker/run.sh: скрипт окружения emsdk при старте контейнера его сбрасывает.
ENV WORK=/work
ENV CCACHE_DIR=/work/ccache

# Каталог исходников на томе принадлежит не тому пользователю, от которого
# git иногда запускается внутри сборки CMake: без этого git отказывается читать.
RUN git config --system --add safe.directory '*'

WORKDIR /work/src
