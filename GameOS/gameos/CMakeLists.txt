cmake_minimum_required (VERSION 3.10)
project(gameos)

set(SOURCES ${SOURCES}
    gameos.cpp
    gameos_graphics.cpp
    gameos_res.cpp
    gameos_fileio.cpp
    gameos_input.cpp
    gameos_debugging.cpp
    gameos_sound.cpp
    gos_render.cpp
    gos_font.cpp
    gos_input.cpp

    utils/stream.cpp
    utils/camera.cpp
    utils/Image.cpp
    utils/logging.cpp
    utils/matrix.cpp
    utils/vec.cpp
    utils/timing.cpp
    utils/string_utils.cpp
    utils/file_utils.cpp
    utils/kv_registry.cpp

    render/render_device.cpp
    render/render_state.cpp
    render/gpu/gpu_device.cpp
    render/render_types.cpp
    render/gl/gl_device.cpp
    render/gl/gl_utils.cpp
    render/gl/gl_render_constants.cpp
    render/gl/shader_builder.cpp
    utils/run_options.cpp
    utils/png_writer.cpp
    utils/input_script.cpp
    )

set(MAIN_SRC gameosmain.cpp)


include_directories("../include"  ".")

# Mod system (C++17 for std::filesystem; headers are C++11). Shared by the
# game, the mc2mod tool and the tests.
add_library(mc2mods mods/mod_system.cpp mods/gos_mods.cpp)
set_target_properties(mc2mods PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(mc2mods PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mods)

add_library(gameos ${SOURCES})
target_link_libraries(gameos MC2::SDL MC2::Mixer mc2mods)
add_library(gameos_main ${MAIN_SRC})
# main() uses the run options / PNG capture helpers from gameos
target_link_libraries(gameos_main gameos windows ZLIB::ZLIB MC2::SDL)

