# Automated tests. Run with:  ctest --output-on-failure
#
#  unit_platform      string helpers and Win32 shims (GameOS/src)
#  unit_user_dir      user data directory and migration from ~/.mechcommander2
#  unit_kv_registry   registry emulation (GameOS/gameos/utils/kv_registry)
#  unit_image_tga     TGA texture decoding (GameOS/gameos/utils/Image)
#  unit_sound         GameOS sound API on SDL3_mixer (no audio device needed)
#  unit_render_state  pipeline keys / cache derived from GameOS render states
#  render_isolation   OpenGL is only used under GameOS/gameos/render/gl/
#  unit_net           LAN transport: connect, ordered reliable delivery, discovery
#  unit_update        update check: versions, release JSON, update.ini, local HTTP
#  unit_savelayout    in-mission save structures keep their sizes
#  unit_mods          mod system: versions, manifests, modpacks, load order, overlay
#  tool_mc2mod        modding workflow through the mc2mod command line tool
#  shader_validation  every shader variant compiles and links (glslangValidator)
#  shader_spirv       every variant compiles to valid SPIR-V and converts to
#                     MSL / HLSL (MC2_SHADER_REQUIRE lists required outputs)
#  unit_stuff         original Stuff library self-tests (mclib/stuff)
#  unit_pqueue        A* open list (mclib/pqueue)
#  unit_run_options   deterministic-mode options, PNG capture writer, clock
#  tool_*             round trips through the data-pack tools
#
# The end-to-end game smoke test needs built game data and a display server,
# so it lives in scripts/smoke_test.sh rather than here.

set(MC2_TEST_FRAMEWORK ${CMAKE_CURRENT_SOURCE_DIR}/framework/mc2test_main.cpp)
set(MC2_GAMEOS_DIR ${CMAKE_SOURCE_DIR}/GameOS/gameos)

function(mc2_add_unit_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
    add_executable(${name} ${T_SOURCES} ${MC2_TEST_FRAMEWORK})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/framework ${T_INCLUDES})
    target_link_libraries(${name} ${T_LIBS})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Small, dependency-free modules are compiled straight into their tests so
# the tests need neither OpenGL nor SDL.
mc2_add_unit_test(unit_platform
    SOURCES unit/test_platform.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/GameOS/include
    LIBS windows)

mc2_add_unit_test(unit_user_dir
    SOURCES unit/test_user_dir.cpp ${MC2_GAMEOS_DIR}/utils/user_dir.cpp
    INCLUDES ${MC2_GAMEOS_DIR})

mc2_add_unit_test(unit_kv_registry
    SOURCES unit/test_kv_registry.cpp ${MC2_GAMEOS_DIR}/utils/kv_registry.cpp
    INCLUDES ${MC2_GAMEOS_DIR})

mc2_add_unit_test(unit_image_tga
    SOURCES unit/test_image_tga.cpp ${MC2_GAMEOS_DIR}/utils/Image.cpp
    INCLUDES ${MC2_GAMEOS_DIR} ${CMAKE_SOURCE_DIR}/GameOS/include
    LIBS windows)

mc2_add_unit_test(unit_run_options
    SOURCES unit/test_run_options.cpp ${MC2_GAMEOS_DIR}/utils/run_options.cpp
            ${MC2_GAMEOS_DIR}/utils/png_writer.cpp ${MC2_GAMEOS_DIR}/utils/input_script.cpp
    INCLUDES ${MC2_GAMEOS_DIR} ${CMAKE_SOURCE_DIR}/GameOS/include
    LIBS windows ZLIB::ZLIB)

mc2_add_unit_test(unit_pqueue
    SOURCES unit/test_pqueue.cpp ${CMAKE_SOURCE_DIR}/mclib/pqueue.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/mclib)

mc2_add_unit_test(unit_stuff
    SOURCES unit/test_stuff_selftests.cpp
    INCLUDES ${COMMON_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/mclib
    LIBS stuff gameos windows MC2::SDL MC2::Mixer ZLIB::ZLIB ${ADDITIONAL_LIBS})

# Data tool round trips (shell based, Linux/macOS only).
if(NOT WIN32)
    set(MC2_TOOLS_DIR ${CMAKE_BINARY_DIR}/out/data_tools)
    foreach(tool_case fastfile rsp pak)
        add_test(NAME tool_${tool_case}
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_data_tools.sh ${tool_case} ${MC2_TOOLS_DIR})
    endforeach()
    add_test(NAME tool_text_tool
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_data_tools.sh text_tool ${CMAKE_BINARY_DIR}/out/text_tool)
    set_tests_properties(tool_text_tool PROPERTIES SKIP_RETURN_CODE 77)
endif()

mc2_add_unit_test(unit_sound
    SOURCES unit/test_sound.cpp
    INCLUDES ${COMMON_INCLUDE_DIRS}
    LIBS gameos windows MC2::SDL MC2::Mixer ZLIB::ZLIB ${ADDITIONAL_LIBS})
set_tests_properties(unit_sound PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")

mc2_add_unit_test(unit_heap
    SOURCES unit/test_heap.cpp
    INCLUDES ${COMMON_INCLUDE_DIRS}
    LIBS gameos windows MC2::SDL MC2::Mixer ZLIB::ZLIB ${ADDITIONAL_LIBS})

mc2_add_unit_test(unit_dice
    SOURCES unit/test_dice.cpp ${CMAKE_SOURCE_DIR}/mclib/routines.cpp
    INCLUDES ${COMMON_INCLUDE_DIRS}
    LIBS gameos windows MC2::SDL MC2::Mixer ZLIB::ZLIB ${ADDITIONAL_LIBS})

mc2_add_unit_test(unit_render_state
    SOURCES unit/test_render_state.cpp ${MC2_GAMEOS_DIR}/render/render_state.cpp
    INCLUDES ${COMMON_INCLUDE_DIRS})

if(NOT WIN32)
    add_test(NAME render_isolation
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_render_isolation.sh ${CMAKE_SOURCE_DIR})
endif()

mc2_add_unit_test(unit_net
    SOURCES unit/test_net.cpp ${MC2_GAMEOS_DIR}/net/gos_net.cpp
    INCLUDES ${MC2_GAMEOS_DIR}
    LIBS MC2::ENet)

find_package(Threads REQUIRED)
mc2_add_unit_test(unit_update
    SOURCES unit/test_update.cpp ${MC2_GAMEOS_DIR}/update/gos_update.cpp
    INCLUDES ${MC2_GAMEOS_DIR}
    LIBS Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(unit_update ws2_32 winhttp)
endif()

# Compiled against the game headers only: sizeof() needs no game code.
mc2_add_unit_test(unit_savelayout
    SOURCES unit/test_savelayout.cpp ${CMAKE_SOURCE_DIR}/code/savelayout.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/code)

mc2_add_unit_test(unit_mods
    SOURCES unit/test_mods.cpp
    LIBS mc2mods)
set_target_properties(unit_mods PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    add_test(NAME tool_mc2mod
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_mc2mod.sh $<TARGET_FILE:mc2mod> ${CMAKE_SOURCE_DIR})
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME shader_validation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/validate_shaders.py ${CMAKE_SOURCE_DIR}/shaders)
    set_tests_properties(shader_validation PROPERTIES SKIP_RETURN_CODE 77)
    # SPIR-V for Vulkan (spirv-val), MSL / HLSL conversion, PerDraw layout
    set(MC2_SHADER_REQUIRE "" CACHE STRING "Shader outputs shader_spirv must produce, e.g. msl,hlsl,dxil")
    set(_require_args)
    if(MC2_SHADER_REQUIRE)
        set(_require_args --require ${MC2_SHADER_REQUIRE})
    endif()
    add_test(NAME shader_spirv
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/build_shaders.py
                ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_CURRENT_BINARY_DIR}/shader_spirv_out
                ${_require_args})
    set_tests_properties(shader_spirv PROPERTIES SKIP_RETURN_CODE 77)
endif()
