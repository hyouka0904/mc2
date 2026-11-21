cmake_minimum_required (VERSION 3.10)
# select GLVND versus legacy OpenGL libraries
cmake_policy(SET CMP0072 NEW) # cmake --help-policy CMP0072
project(mc2)

include(CMakeToolsHelpers OPTIONAL)

# Default to an optimized build with debug info for single-configuration
# generators; without this a plain "cmake .." produced an unoptimized (-O0)
# game.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 11)
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
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)



get_filename_component(COM_PATH1 "./GameOS/include" ABSOLUTE)
get_filename_component(COM_PATH2 "./GameOS/gameos" ABSOLUTE)

set(COMMON_INCLUDE_DIRS  ${COM_PATH1} ${COM_PATH2})

add_subdirectory("./mclib/" "./out/mclib")
add_subdirectory("./mclib/mlr" "./out/mclib/mlr")
add_subdirectory("./mclib/gosfx" "./out/mclib/gosfx")
add_subdirectory("./mclib/stuff" "./out/mclib/stuff")
add_subdirectory("./gui" "./out/gui")
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
    "code/mc2movie.cpp"
    )


include_directories(${COMMON_INCLUDE_DIRS} "./mclib" "./gui" "./code" "./netlib")

if(NOT WIN32)
	set(ADDITIONAL_LIBS dl)
else()
	# winmm for timeGetTime, maybe switch to GetTickCount to remove this dependency
	set(ADDITIONAL_LIBS winmm)
endif()


add_executable(mc2 ${SOURCES} ${MAIN_SRC})
target_link_libraries(mc2 mclib gosfx mlr stuff gui gameos gameos_main windows ZLIB::ZLIB MC2::SDL GLEW::GLEW MC2::Mixer ${ADDITIONAL_LIBS} OpenGL::GL)

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
add_subdirectory("./Viewer" "./out/Viewer")

option(MC2_BUILD_TESTS "Build the automated tests (run with ctest)" ON)
if(MC2_BUILD_TESTS)
    enable_testing()
    add_subdirectory("./tests" "./out/tests")
endif()
