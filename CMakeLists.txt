cmake_minimum_required(VERSION 3.9)

if (${TARGET_EMSCRIPTEN})
    set(CMAKE_TOOLCHAIN_FILE ${EMSCRIPTEN_PATH}/cmake/Modules/Platform/Emscripten.cmake)
endif()

project(wrike-imgui)

if (${TARGET_EMSCRIPTEN})
elseif(${TARGET_OSX})
elseif(${TARGET_SDL})
else()
    message(FATAL_ERROR "No target specified")
endif()

if (${TARGET_EMSCRIPTEN})
elseif(${TARGET_OSX})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

    include_directories("/usr/include")
elseif(${TARGET_SDL})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")
    set(CURL_LIBRARY "-lcurl")

    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(CURL REQUIRED)

    include_directories(${SDL2_INCLUDE_DIRS})
    include_directories(${OPENGL_INCLUDE_DIRS})
    include_directories(${CURL_INCLUDE_DIR})

    include_directories("/usr/include")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out)
set(CMAKE_BUILD_TYPE_INIT "Debug")
set(CMAKE_CXX_STANDARD 11)

include_directories(libc)
include_directories(external)



set(EXTERNAL_SOURCES
        external/imgui.cpp
        external/imgui_demo.cpp
        external/imgui_draw.cpp

        external/imgui.h
        external/imgui_internal.h
        external/imconfig.h

        external/jsmn.c
        external/jsmn.h

        external/xxhash.c
        external/xxhash.h

        external/html_entities.c
        external/html_entities.h
)

set(SOURCE_FILES
        src/json.cpp
        src/json.h

        src/hash_map.h
        src/id_hash_map.h

        src/folder_tree.cpp
        src/folder_tree.h

        src/temporary_storage.cpp
        src/temporary_storage.h

        src/rich_text.cpp
        src/rich_text.h

        src/header.cpp
        src/header.h

        src/ui.cpp
        src/ui.h

        src/task_list.cpp
        src/task_list.h

        src/task_view.cpp
        src/task_view.h

        src/render_rich_text.cpp
        src/render_rich_text.h

        src/base32.c
        src/base32.h

#        src/sdf.cpp
#        src/sdf.h

        src/common.cpp
        src/common.h

        src/lazy_array.h

        src/tracing.cpp
        src/tracing.h

        src/platform.h

        src/account.cpp
        src/account.h

        src/users.cpp
        src/users.h

        src/workflows.cpp
        src/workflows.h

        src/custom_fields.cpp
        src/custom_fields.h

        src/inbox.cpp
        src/inbox.h

        src/main.cpp
        src/main.h
)

if (${TARGET_EMSCRIPTEN})
    add_definitions(-DEMSCRIPTEN=1)

    set(SOURCE_FILES ${SOURCE_FILES} src/platform_emscripten.cpp)
elseif(${TARGET_OSX})
    set(SOURCE_FILES ${SOURCE_FILES} src/platform_osx.mm)
elseif(${TARGET_SDL})
    add_definitions(-DEMSCRIPTEN=0)

    set(SOURCE_FILES ${SOURCE_FILES} src/platform_sdl.cpp)

    set(EXTERNAL_SOURCES ${EXTERNAL_SOURCES}
            external/lodepng.c
            external/lodepng.h
    )
endif()

add_library(external ${EXTERNAL_SOURCES})

add_definitions(-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS)
add_definitions(-DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0)
add_definitions(-DIMGUI_DISABLE_DEMO_WINDOWS)
add_definitions(-DJSMN_PARENT_LINKS)
add_definitions(-DLODEPNG_NO_COMPILE_ENCODER)
add_definitions(-DLODEPNG_NO_COMPILE_ERROR_TEXT)
add_definitions(-fno-exceptions)
add_definitions(-fno-rtti)


add_custom_command(
        OUTPUT out/resources.js
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND "python ${EMSCRIPTEN_PATH}/tools/file_packager.py out/resources.data --preload resources > out/resources.js"
        COMMENT "Packaging resources"
)

set(LINK_LIBRARIES external)

if (${TARGET_EMSCRIPTEN})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=1 -s FORCE_FILESYSTEM=1 -s TOTAL_MEMORY=64MB -s ASSERTIONS=1" )

    if (${DEBUG})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g4 -s ASSERTIONS=1")
    endif()

    if (${ENABLE_SANITIZER})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ASSERTIONS=2 -s SAFE_HEAP=1 -s STACK_OVERFLOW_CHECK=2")
    endif()

    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_CXX_FLAGS} --bind --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/out/resources.js" )
    add_definitions("-DEXPORT=__attribute__((used))")
elseif(${TARGET_OSX})
    if (${ENABLE_SANITIZER})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address -fno-omit-frame-pointer")
    endif()

    add_definitions(-DEXPORT= )
elseif(${TARGET_SDL})
    if (${ENABLE_SANITIZER})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address -fno-omit-frame-pointer")
    endif()

    set(LINK_LIBRARIES ${LINK_LIBRARIES} ${SDL2_LIBRARY} ${OPENGL_LIBRARIES} ${CURL_LIBRARIES})
    add_definitions(-DEXPORT= )
endif()

if (${DEBUG})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
endif()

# Jesus that hurt me so many times already
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -Wall")

include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)


add_executable(wrike-imgui ${SOURCE_FILES})

if(lto_supported)
    message(STATUS "IPO / LTO enabled")
    #set_property(TARGET wrike-imgui PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

target_link_libraries(wrike-imgui ${LINK_LIBRARIES})

add_custom_target(
        resources DEPENDS out/resources.js
)

add_custom_command(COMMAND python
        ${CMAKE_CURRENT_SOURCE_DIR}/generate_out.py
        ${CMAKE_CURRENT_SOURCE_DIR}/out/shell.html
        ${CMAKE_CURRENT_SOURCE_DIR}/out/wrike.html
        ${CMAKE_CURRENT_SOURCE_DIR}/out/wrike-imgui.js
        OUTPUT wrike-imgui.js
        DEPENDS wrike-imgui
        COMMENT "Updating build time")