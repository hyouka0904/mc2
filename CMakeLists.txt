cmake_minimum_required (VERSION 3.10)
# select GLVND versus legacy OpenGL libraries
cmake_policy(SET CMP0072 NEW) # cmake --help-policy CMP0072
# Oldest supported macOS (decision record: macOS 13); must precede project()
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
project(mc2)

include(CMakeToolsHelpers OPTIONAL)

# Default to an optimized build with debug info for single-configuration
# generators; without this a plain "cmake .." produced an unoptimized (-O0)
# game.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
	message(STATUS "Target is 64 bits")
	set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS TRUE)
else("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
	message(STATUS "Target is 32 bits")
endif("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")

set(CMAKE_EXPORT_COMPILE_COMMANDS "ON")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(NOT MSVC)
    # -Wno-unused-local-typedefs: "typedef was ignored on this declaration"
    # -Wno-error=array-bounds: newer GCCs flag legacy code that older ones did not
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedefs -Wno-error=array-bounds -Wno-switch -Wwrite-strings -Wno-unknown-warning-option -Wunused-variable")
    option(MC2_ENABLE_ASAN "Build with AddressSanitizer" OFF)
    if(MC2_ENABLE_ASAN)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    endif()
endif()

if(MSVC)
	add_definitions(-DPLATFORM_WINDOWS)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	message("Compiling on M$ Windows")
endif()

# to support windows sln file generator as well
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_ARMOR -D_DEBUG -DBUGLOG -DLAB_ONLY")
#set(CMAKE_CXX_FLAGS_RELWITHDEBINFO" ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DSOME_STUFF")
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()


add_definitions(-DUSE_ASSEMBLER_CODE=0)
add_definitions(-DLINUX_BUILD)

# SDL3 / SDL3_mixer / SDL3_ttf -> MC2::SDL, MC2::Mixer, MC2::TTF
include(MC2Dependencies)
# The OpenGL backend (-renderer gl) is kept for one release as a fallback
# for the SDL3 GPU backend. macOS only has a deprecated OpenGL 4.1, so it is
# off there by default.
if(APPLE)
    set(_mc2_gl_default OFF)
else()
    set(_mc2_gl_default ON)
endif()
option(MC2_ENABLE_OPENGL "Build the OpenGL backend (needs GLEW)" ${_mc2_gl_default})
if(MC2_ENABLE_OPENGL)
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)
else()
    add_definitions(-DMC2_NO_OPENGL)
endif()
find_package(ZLIB REQUIRED)



get_filename_component(COM_PATH1 "./GameOS/include" ABSOLUTE)
get_filename_component(COM_PATH2 "./GameOS/gameos" ABSOLUTE)

set(COMMON_INCLUDE_DIRS  ${COM_PATH1} ${COM_PATH2})

# Version shown in the window title and by -version: -DMC2_VERSION=... (the
# release workflow passes the tag), else `git describe`, else 0.0.0-dev.
set(MC2_VERSION "" CACHE STRING "Version string (default: git describe)")
set(MC2_VERSION_STRING "${MC2_VERSION}")
if(NOT MC2_VERSION_STRING)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE MC2_VERSION_STRING OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
endif()
if(NOT MC2_VERSION_STRING)
    set(MC2_VERSION_STRING "0.0.0-dev")
endif()
string(REGEX REPLACE "^v" "" MC2_VERSION_STRING "${MC2_VERSION_STRING}")
# x.y.z for places that need a number (macOS bundle version)
if(MC2_VERSION_STRING MATCHES "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?")
    set(MC2_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
    if(CMAKE_MATCH_4)
        string(APPEND MC2_VERSION_NUMERIC ".${CMAKE_MATCH_4}")
    endif()
else()
    set(MC2_VERSION_NUMERIC "0.0.0")
endif()
message(STATUS "Version: ${MC2_VERSION_STRING}")
configure_file(${CMAKE_SOURCE_DIR}/cmake/mc2_version.h.in ${CMAKE_BINARY_DIR}/generated/mc2_version.h)
include_directories(${CMAKE_BINARY_DIR}/generated)

add_subdirectory("./mclib/" "./out/mclib")
add_subdirectory("./mclib/mlr" "./out/mclib/mlr")
add_subdirectory("./mclib/gosfx" "./out/mclib/gosfx")
add_subdirectory("./mclib/stuff" "./out/mclib/stuff")
add_subdirectory("./gui" "./out/gui")
add_subdirectory("./thirdparty/enet" "./out/thirdparty/enet")
add_subdirectory("./GameOS/gameos" "./out/GameOS/gameos")

add_subdirectory("./GameOS/src" "./out/windows")

set(SOURCES 
    "code/dw.cpp"
    "code/mechcmd2.cpp"
    "code/artlry.cpp"
    "code/bldng.cpp"
    "code/carnage.cpp"
    "code/collsn.cpp"
    "code/comndr.cpp"
    "code/contact.cpp"
    "code/gameobj.cpp"
    "code/gate.cpp"
    "code/goal.cpp"
    "code/group.cpp"
    "code/gvehicl.cpp"
    "code/light.cpp"
    "code/mech.cpp"
    "code/mover.cpp"
    "code/objmgr.cpp"
    "code/objtype.cpp"
    "code/tacordr.cpp"
    "code/team.cpp"
    "code/terrobj.cpp"
    "code/turret.cpp"
    "code/warrior.cpp"
    "code/weaponbolt.cpp"
    "code/ablmc2.cpp"
    "code/gamecam.cpp"
    "code/mission.cpp"
    "code/mission2.cpp"
    "code/objective.cpp"
    "code/saveload.cpp"
    "code/savelayout.cpp"
    "code/trigger.cpp"
    "code/weather.cpp"
    "gui/aanimobject.cpp"
    "gui/aedit.cpp"
    "code/attributemeter.cpp"
    "code/controlgui.cpp"
    "code/forcegroupbar.cpp"
    "code/gametacmap.cpp"
    "code/infowindow.cpp"
    "code/keyboardref.cpp"
    "code/mechicon.cpp"
    "code/missiongui.cpp"
    "code/mpconnectiontype.cpp"
    "code/lanaddresses.cpp"
    "code/mpgamebrowser.cpp"
    "code/mphostgame.cpp"
    "code/mploadmap.cpp"
    "code/mpparameterscreen.cpp"
    "code/mpsetuparea.cpp"
    "code/optionsarea.cpp"
    "code/optionsscreenwrapper.cpp"
    "code/pausewindow.cpp"
    "code/prefs.cpp"
    "code/simplecamera.cpp"
    "code/componentlistbox.cpp"
    "code/loadscreen.cpp"
    "code/logistics.cpp"
    "code/logisticscomponent.cpp"
    "code/logisticsdata.cpp"
    "code/logisticsdialog.cpp"
    "code/logisticsmech.cpp"
    "code/logisticsmechdisplay.cpp"
    "code/logisticsmechicon.cpp"
    "code/logisticsmissioninfo.cpp"
    "code/logisticspilot.cpp"
    "code/logisticspilotlistbox.cpp"
    "gui/logisticsscreen.cpp"
    "code/logisticsvariant.cpp"
    "code/mainmenu.cpp"
    "code/mechbayscreen.cpp"
    "code/mechlabscreen.cpp"
    "code/mechlistbox.cpp"
    "code/mechlopedia.cpp"
    "code/mechpurchasescreen.cpp"
    "code/missionbegin.cpp"
    "code/missionbriefingscreen.cpp"
    "code/missionresults.cpp"
    "code/missionselectionscreen.cpp"
    "code/mpprefs.cpp"
    "code/pilotreadyscreen.cpp"
    "code/pilotreviewarea.cpp"
    "code/salvagemecharea.cpp"
    "code/simplecomponentlistbox.cpp"
    "code/gamesound.cpp"
    "code/radio.cpp"
    "code/movemgr.cpp"
    "code/chatwindow.cpp"
    "code/mpstats.cpp"
    "code/multplyr.cpp"
    "code/multplyr_sync.cpp"
    "code/mc2movie.cpp"
    )


include_directories(${COMMON_INCLUDE_DIRS} "./mclib" "./gui" "./code" "./netlib" "./thirdparty")

if(NOT WIN32)
	set(ADDITIONAL_LIBS dl)
else()
	# winmm for timeGetTime, maybe switch to GetTickCount to remove this dependency
	set(ADDITIONAL_LIBS winmm)
endif()


# Windows: icon and version resource; a GUI program (no console window),
# still with main() as the entry point
if(WIN32)
    string(REPLACE "." "," MC2_VERSION_RC "${MC2_VERSION_NUMERIC},0")
    file(WRITE ${CMAKE_BINARY_DIR}/generated/mc2_version_rc.h
        "#define MC2_VERSION_RC ${MC2_VERSION_RC}\n#define MC2_VERSION_STR \"${MC2_VERSION_STRING}\"\n#define MC2_ICON \"${CMAKE_SOURCE_DIR}/packaging/icon.ico\"\n")
    list(APPEND MAIN_SRC ${CMAKE_SOURCE_DIR}/packaging/windows/mc2.rc)
endif()

add_executable(mc2 ${SOURCES} ${MAIN_SRC})
if(MSVC)
    target_link_options(mc2 PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    # The Visual C++ runtime DLLs, listed for scripts/package.py (they are
    # shipped next to mc2.exe, so players need no separate installer)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    string(REPLACE ";" "\n" MC2_RUNTIME_LIBS "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    file(WRITE ${CMAKE_BINARY_DIR}/runtime_libs.txt "${MC2_RUNTIME_LIBS}\n")
endif()
target_link_libraries(mc2 mclib gosfx mlr stuff gui gameos gameos_main windows ZLIB::ZLIB MC2::SDL MC2::Mixer ${ADDITIONAL_LIBS})
if(MC2_ENABLE_OPENGL)
    target_link_libraries(mc2 gameos_gl)
endif()

# macOS: mc2.app. The string resource library goes into Contents/Resources,
# where the game looks for it first; the game data can sit next to the .app
# (or be the current directory, as on the other platforms).
if(APPLE)
    set_target_properties(mc2 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "MechCommander 2"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mc2.mechcommander2"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MC2_VERSION_NUMERIC}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MC2_VERSION_NUMERIC}")
    add_dependencies(mc2 mc2res_64)
    add_custom_command(TARGET mc2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:mc2>/Resources
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mc2res_64> $<TARGET_BUNDLE_CONTENT_DIR:mc2>/Resources/libmc2res.dylib
        VERBATIM)
endif()

# Shaders for the non-OpenGL backends (SPIR-V, MSL, HLSL, DXIL when dxc is
# installed) are compiled into ${CMAKE_BINARY_DIR}/shaders_out at build time.
# The OpenGL backend compiles shaders/ at run time and does not need them.
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(MC2_GLSLANG glslangValidator)
find_program(MC2_SPIRV_VAL spirv-val)
if(Python3_FOUND AND MC2_GLSLANG AND MC2_SPIRV_VAL)
    file(GLOB MC2_SHADER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/shaders/*.vert ${CMAKE_SOURCE_DIR}/shaders/*.frag
        ${CMAKE_SOURCE_DIR}/shaders/include/*.hglsl)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders_out/shaders.json
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/build_shaders.py
                ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/shaders_out
        DEPENDS ${MC2_SHADER_SOURCES} ${CMAKE_SOURCE_DIR}/scripts/build_shaders.py
        COMMENT "Compiling shaders to SPIR-V / MSL / HLSL"
        VERBATIM)
    add_custom_target(mc2_shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/shaders_out/shaders.json)
else()
    message(STATUS "glslangValidator / spirv-val not found: SPIR-V shaders are not built")
endif()

add_subdirectory("./res" "./out/res")
add_subdirectory("./data_tools" "./out/data_tools")
add_subdirectory("./text_tool" "./out/text_tool")
add_subdirectory("./tools/mc2mod" "./out/tools/mc2mod")

option(MC2_BUILD_TESTS "Build the automated tests (run with ctest)" ON)
if(MC2_BUILD_TESTS)
    enable_testing()
    add_subdirectory("./tests" "./out/tests")
endif()
