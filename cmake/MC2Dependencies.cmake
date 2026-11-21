# SDL3, SDL3_mixer and SDL3_ttf.
#
# Installed packages are used when CMake can find them (set CMAKE_PREFIX_PATH
# or SDL3_DIR for a custom location). Otherwise the pinned releases below are
# downloaded and built as static libraries, so nothing has to be installed
# and the game binaries do not depend on SDL shared libraries.
#
#   -DMC2_SDL3_SOURCE=AUTO     (default) installed packages, else download
#   -DMC2_SDL3_SOURCE=SYSTEM   installed packages only, fail if missing
#   -DMC2_SDL3_SOURCE=FETCH    always download
#
# The rest of the build links against MC2::SDL, MC2::Mixer and MC2::TTF.

set(MC2_SDL3_SOURCE "AUTO" CACHE STRING "Where SDL3 comes from: AUTO, SYSTEM or FETCH")
set_property(CACHE MC2_SDL3_SOURCE PROPERTY STRINGS AUTO SYSTEM FETCH)

set(MC2_SDL3_TAG       "release-3.4.8")
set(MC2_SDL3_MIXER_TAG "release-3.2.4")
set(MC2_SDL3_TTF_TAG   "release-3.2.2")

if(NOT MC2_SDL3_SOURCE STREQUAL "FETCH")
    find_package(SDL3 3.2 CONFIG QUIET)
    find_package(SDL3_mixer 3.2 CONFIG QUIET)
    find_package(SDL3_ttf 3.2 CONFIG QUIET)
endif()

if(SDL3_FOUND AND SDL3_mixer_FOUND AND SDL3_ttf_FOUND)
    message(STATUS "SDL3: using installed SDL3 ${SDL3_VERSION}, SDL3_mixer ${SDL3_mixer_VERSION}, SDL3_ttf ${SDL3_ttf_VERSION}")
    set(_mc2_sdl SDL3::SDL3)
    set(_mc2_mixer SDL3_mixer::SDL3_mixer)
    set(_mc2_ttf SDL3_ttf::SDL3_ttf)
elseif(MC2_SDL3_SOURCE STREQUAL "SYSTEM")
    message(FATAL_ERROR "MC2_SDL3_SOURCE=SYSTEM but SDL3 / SDL3_mixer / SDL3_ttf were not found")
else()
    message(STATUS "SDL3: building ${MC2_SDL3_TAG}, SDL3_mixer ${MC2_SDL3_MIXER_TAG}, SDL3_ttf ${MC2_SDL3_TTF_TAG} from source")
    include(FetchContent)

    # Static libraries; options are set as cache variables before the
    # projects are configured.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

    # The game only plays WAVE files: no codec libraries needed.
    foreach(opt VENDORED TESTS EXAMPLES INSTALL FLAC GME MOD MP3 MIDI OPUS
                VORBIS_STB VORBIS_VORBISFILE WAVPACK)
        set(SDLMIXER_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()

    # Fonts are baked with the system FreeType; no shaping or SVG glyphs.
    foreach(opt VENDORED SAMPLES INSTALL HARFBUZZ PLUTOSVG)
        set(SDLTTF_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()

    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${MC2_SDL3_TAG} GIT_SHALLOW TRUE)
    FetchContent_Declare(SDL3_mixer GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
        GIT_TAG ${MC2_SDL3_MIXER_TAG} GIT_SHALLOW TRUE)
    FetchContent_Declare(SDL3_ttf GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG ${MC2_SDL3_TTF_TAG} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3 SDL3_mixer SDL3_ttf)

    # Static targets are named <pkg>-static by some releases; plain otherwise
    foreach(pair "_mc2_sdl;SDL3::SDL3" "_mc2_mixer;SDL3_mixer::SDL3_mixer" "_mc2_ttf;SDL3_ttf::SDL3_ttf")
        list(GET pair 0 var)
        list(GET pair 1 tgt)
        if(TARGET ${tgt}-static)
            set(${var} ${tgt}-static)
        elseif(TARGET ${tgt})
            set(${var} ${tgt})
        else()
            message(FATAL_ERROR "SDL3: target ${tgt} missing after FetchContent")
        endif()
    endforeach()
endif()

add_library(mc2_sdl INTERFACE)
target_link_libraries(mc2_sdl INTERFACE ${_mc2_sdl})
add_library(MC2::SDL ALIAS mc2_sdl)

add_library(mc2_mixer INTERFACE)
target_link_libraries(mc2_mixer INTERFACE ${_mc2_mixer} mc2_sdl)
add_library(MC2::Mixer ALIAS mc2_mixer)

add_library(mc2_ttf INTERFACE)
target_link_libraries(mc2_ttf INTERFACE ${_mc2_ttf} mc2_sdl)
add_library(MC2::TTF ALIAS mc2_ttf)
