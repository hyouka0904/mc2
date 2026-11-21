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
    utils/gl_utils.cpp
    utils/gl_render_constants.cpp
    utils/Image.cpp
    utils/logging.cpp
    utils/shader_builder.cpp
    utils/matrix.cpp
    utils/vec.cpp
    utils/timing.cpp
    utils/string_utils.cpp
    utils/file_utils.cpp
    utils/kv_registry.cpp
    utils/run_options.cpp
    utils/png_writer.cpp
    utils/input_script.cpp
    )

set(MAIN_SRC gameosmain.cpp)


include_directories("../include"  ".")

add_library(gameos ${SOURCES})
target_link_libraries(gameos MC2::SDL MC2::Mixer)
add_library(gameos_main ${MAIN_SRC})
# main() uses the run options / PNG capture helpers from gameos
target_link_libraries(gameos_main gameos windows ZLIB::ZLIB MC2::SDL)

