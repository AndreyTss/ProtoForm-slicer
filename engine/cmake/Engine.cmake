# Исполняемые цели поверх libslic3r:
#  * smoke — проверка сборки (engine/smoke), запускается в Node;
#  * engine — модуль движка из engine/bridge/*.cpp: engine.mjs + engine.wasm.

# Флаги, общие для всего, что компонуется с libslic3r.
set(ENGINE_COMMON_LINK_OPTIONS
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=128MB
    -sMAXIMUM_MEMORY=4GB
    # У потоков OrcaSlicer стек 16 МБ; в WebAssembly нет защитной страницы,
    # и глубокая рекурсия Arachne и CGAL молча портила бы память.
    -sSTACK_SIZE=16MB
    -sMALLOC=dlmalloc)

add_executable(smoke "${ENGINE_DIR}/smoke/smoke.cpp")
target_link_libraries(smoke PRIVATE libslic3r)
target_link_options(smoke PRIVATE ${ENGINE_COMMON_LINK_OPTIONS} -sENVIRONMENT=node -sEXIT_RUNTIME=1)
set_target_properties(smoke PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")

file(GLOB ENGINE_BRIDGE_SOURCES CONFIGURE_DEPENDS "${ENGINE_DIR}/bridge/*.cpp")
if (NOT ENGINE_BRIDGE_SOURCES)
    message(STATUS "В engine/bridge нет .cpp: цель engine не создаётся, есть libslic3r и smoke.")
    return()
endif ()

add_executable(engine ${ENGINE_BRIDGE_SOURCES})
target_include_directories(engine PRIVATE "${ENGINE_DIR}/bridge")
target_link_libraries(engine PRIVATE libslic3r boost_libs)
target_link_options(engine PRIVATE
    ${ENGINE_COMMON_LINK_OPTIONS}
    # ES-модуль с фабрикой: const Module = await createEngine({...}).
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createEngine
    -sENVIRONMENT=web,worker,node
    -lembind
    -sEXPORTED_RUNTIME_METHODS=FS
    ${ENGINE_EXTRA_LINK_OPTIONS})
set_target_properties(engine PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")

# Мост может добавить свои настройки (встроенные файлы, экспорт) в
# engine/bridge/link.cmake, не трогая этот файл.
if (EXISTS "${ENGINE_DIR}/bridge/link.cmake")
    include("${ENGINE_DIR}/bridge/link.cmake")
endif ()
