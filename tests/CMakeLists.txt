# Automated tests. Run with:  ctest --output-on-failure
#
#  unit_platform      string helpers and Win32 shims (GameOS/src)
#  unit_kv_registry   registry emulation (GameOS/gameos/utils/kv_registry)
#  unit_image_tga     TGA texture decoding (GameOS/gameos/utils/Image)
#  unit_stuff         original Stuff library self-tests (mclib/stuff)
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

mc2_add_unit_test(unit_kv_registry
    SOURCES unit/test_kv_registry.cpp ${MC2_GAMEOS_DIR}/utils/kv_registry.cpp
    INCLUDES ${MC2_GAMEOS_DIR})

mc2_add_unit_test(unit_image_tga
    SOURCES unit/test_image_tga.cpp ${MC2_GAMEOS_DIR}/utils/Image.cpp
    INCLUDES ${MC2_GAMEOS_DIR} ${CMAKE_SOURCE_DIR}/GameOS/include
    LIBS windows)

mc2_add_unit_test(unit_stuff
    SOURCES unit/test_stuff_selftests.cpp
    INCLUDES ${COMMON_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/mclib
    LIBS stuff gameos windows SDL2::Main SDL2::Mixer GLEW::GLEW OpenGL::GL ZLIB::ZLIB ${ADDITIONAL_LIBS})

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
