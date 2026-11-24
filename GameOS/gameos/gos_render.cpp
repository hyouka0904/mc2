#include "gos_render.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif
#include <stdio.h>
#include <assert.h>
#include <SDL3/SDL.h>
#include "utils/logging.h"
#include "render/render_device.h"

// The input code needs the window for SDL_WarpMouseInWindow
SDL_Window* g_sdl_window = NULL;

namespace graphics {

static bool VERBOSE_VIDEO = true;
static bool VERBOSE_RENDER = true;
static bool VERBOSE_MODES = true;
static bool ENABLE_VSYNC = true;

struct RenderWindow {
    SDL_Window* window_;
    int width_;
    int height_;
};

struct RenderContext {
   SDL_GLContext glcontext_;
   RenderWindow* render_window_;
};

// GameOS addresses displays by index; SDL3 by SDL_DisplayID.
static SDL_DisplayID display_id_from_index(int index)
{
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    SDL_DisplayID id = (ids && index >= 0 && index < count) ? ids[index] : 0;
    SDL_free(ids);
    return id;
}

static int bits_per_pixel(SDL_PixelFormat format)
{
    return SDL_BITSPERPIXEL(format);
}

//==============================================================================
void set_verbose(bool is_verbose)
{
    VERBOSE_VIDEO = is_verbose;
    VERBOSE_RENDER = is_verbose;
    VERBOSE_MODES = is_verbose;
}

//==============================================================================
RenderWindow* create_window(const char* pwinname, int width, int height)
{
    if (VERBOSE_VIDEO) {
        const int n = SDL_GetNumVideoDrivers();
        fprintf(stderr, "Built-in video drivers:");
        for (int i = 0; i < n; ++i)
            fprintf(stderr, "%s %s", i ? "," : "", SDL_GetVideoDriver(i));
        fprintf(stderr, "\n");
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Couldn't initialize video driver: %s\n", SDL_GetError());
        return NULL;
    }
    if (VERBOSE_VIDEO) {
        const int v = SDL_GetVersion();
        fprintf(stderr, "Video driver: %s\n", SDL_GetCurrentVideoDriver());
        fprintf(stderr, "SDL %d.%d.%d (compiled against %d.%d.%d), revision %s\n",
                SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
                SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetRevision());
    }

    const bool use_gl = gos_render::backend() == gos_render::Backend::GL;
    if (use_gl) {
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    if (VERBOSE_MODES) {
        int n = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&n);
        fprintf(stderr, "Number of displays: %d\n", n);
        for (int i = 0; i < n; ++i) {
            const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[i]);
            if (mode)
                fprintf(stderr, "Display %d: %dx%d@%.0fHz, %d bpp (%s)\n", i, mode->w, mode->h,
                        mode->refresh_rate, bits_per_pixel(mode->format),
                        SDL_GetPixelFormatName(mode->format));
            int m = 0;
            SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displays[i], &m);
            for (int j = 0; j < m; ++j)
                fprintf(stderr, "    Mode %d: %dx%d@%.0fHz, %d bpp\n", j, modes[j]->w, modes[j]->h,
                        modes[j]->refresh_rate, bits_per_pixel(modes[j]->format));
            SDL_free(modes);
        }
        SDL_free(displays);
    }

    SDL_Window* window = SDL_CreateWindow(pwinname ? pwinname : "--", width, height,
                                          (use_gl ? SDL_WINDOW_OPENGL : 0) | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        fprintf(stderr, "Couldn't create window: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GetWindowSize(window, &width, &height);
    SDL_ShowWindow(window);

    RenderWindow* rw = new RenderWindow();
    rw->window_ = window;
    rw->width_ = width;
    rw->height_ = height;

    g_sdl_window = window;

    return rw;
}

//==============================================================================
void swap_window(RenderWindowHandle h)
{
    RenderWindow* rw = (RenderWindow*)h;
    assert(rw && rw->window_);
    (void)rw;
    gos_render::device()->present();
}

//==============================================================================
RenderContextHandle init_render_context(RenderWindowHandle render_window)
{
    RenderWindow* rw = (RenderWindow*)render_window;
    assert(rw && rw->window_);

    if (gos_render::backend() != gos_render::Backend::GL) {
        // SDL3 GPU claims the window itself (render/gpu/gpu_device.cpp)
        RenderContext* rc = new RenderContext();
        rc->glcontext_ = NULL;
        rc->render_window_ = render_window;
        return rc;
    }

    SDL_GLContext glcontext = SDL_GL_CreateContext(rw->window_);
    if (!glcontext ) {
        fprintf(stderr, "SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return NULL;
    }

    if (!SDL_GL_MakeCurrent(rw->window_, glcontext)) {
        fprintf(stderr, "SDL_GL_MakeCurrent(): %s\n", SDL_GetError());
        SDL_GL_DestroyContext(glcontext);
        return NULL;
    }

    if (ENABLE_VSYNC) {
        SDL_GL_SetSwapInterval(1);
    } else {
        SDL_GL_SetSwapInterval(0);
    }

    if(VERBOSE_RENDER) {
        int value = 0;
        if (SDL_GL_GetAttribute(SDL_GL_ACCELERATED_VISUAL, &value))
            printf("SDL_GL_ACCELERATED_VISUAL: %d\n", value);
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &value))
            printf("SDL_GL_CONTEXT_MAJOR_VERSION: %d\n", value);
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &value))
            printf("SDL_GL_CONTEXT_MINOR_VERSION: %d\n", value);
    }

    RenderContext* rc = new RenderContext();
    rc->glcontext_ = glcontext;
    rc->render_window_ = render_window;

	return rc;
}

//==============================================================================
void destroy_render_context(RenderContextHandle rc_handle)
{
    RenderContext* rc = (RenderContext*)rc_handle;
    assert(rc);

    if (rc->glcontext_)
        SDL_GL_DestroyContext(rc->glcontext_);
    delete rc;
}

//==============================================================================
void make_current_context(RenderContextHandle ctx_h)
{
    RenderContext* rc = (RenderContext*)ctx_h;
    assert(rc && rc->render_window_);
    if (!rc->glcontext_)
        return;

    RenderWindow* rw = rc->render_window_;
    assert(rw && rw->window_);

    SDL_GL_MakeCurrent(rw->window_, rc->glcontext_);
}

//==============================================================================
bool resize_window(RenderWindowHandle rw_handle, int width, int height)
{
    RenderWindow* rw = (RenderWindow*)rw_handle;
    assert(rw);

    SDL_SetWindowSize(rw->window_, width, height);
    SDL_SyncWindow(rw->window_); // SDL3 applies window changes asynchronously
    rw->width_ = width;
    rw->height_ = height;

    return true;
}

//==============================================================================
bool set_window_fullscreen(RenderWindowHandle rw_handle, bool fullscreen)
{
    RenderWindow* rw = (RenderWindow*)rw_handle;
    assert(rw);

    // NULL fullscreen mode = borderless desktop-sized window
    SDL_SetWindowFullscreenMode(rw->window_, NULL);
    if(!SDL_SetWindowFullscreen(rw->window_, fullscreen)) {
        log_error("SDL_SetWindowFullscreen: %s\n", SDL_GetError());
        return false;
    }
    // Callers read the drawable size right after this; wait until the window
    // manager has applied the change (SDL3 is asynchronous here).
    SDL_SyncWindow(rw->window_);
    return true;
}

//==============================================================================
bool is_mode_supported(int display_index, int width, int height, int bpp) {

    // Check the display the window is on: with several monitors the options
    // screen lists that display's modes, so validating them against the
    // primary display could reject (or accept) the wrong ones.
    SDL_DisplayID display = display_id_from_index(display_index);
    if (!display)
        display = SDL_GetPrimaryDisplay();
    SDL_DisplayMode closest;
    if(!SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.0f, false, &closest)) {
        log_error("is_mode_supported: %s\n", SDL_GetError());
        return false;
    }
    // SDL3 reports 32-bit desktop formats; the game asks for 16 or 32 bpp
    // and renders the same either way, so only the size has to match.
    (void)bpp;
    return closest.w == width && closest.h == height;
}

//==============================================================================
int get_window_display_index(RenderContextHandle ctx_h)
{
    RenderContext* rc = (RenderContext*)ctx_h;
    assert(rc);

    RenderWindow* rw = rc->render_window_;
    assert(rw && rw->window_);

    const SDL_DisplayID id = SDL_GetDisplayForWindow(rw->window_);
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    int index = 0;
    for (int i = 0; i < count; ++i)
        if (ids[i] == id)
            index = i;
    SDL_free(ids);
    return index;
}

//==============================================================================
int get_num_display_modes(int display_index)
{
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display_id_from_index(display_index), &count);
    SDL_free(modes);
    return count;
}

//==============================================================================
bool get_desktop_display_mode(int display_index, int* width, int* height, int* bpp)
{
    assert(width && height && bpp);

    const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(display_id_from_index(display_index));
    if (!dm) {
        log_error("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        return false;
    }

    *width = dm->w;
    *height = dm->h;
    *bpp = bits_per_pixel(dm->format);
    return true;
}

//==============================================================================
bool get_display_mode_by_index(int display_index, int mode_index, int* width, int* height, int* bpp)
{
    assert(width && height && bpp);

    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display_id_from_index(display_index), &count);
    if (!modes || mode_index < 0 || mode_index >= count) {
        log_error("get_display_mode_by_index(%d, %d) failed: %s", display_index, mode_index, SDL_GetError());
        SDL_free(modes);
        return false;
    }

    *width = modes[mode_index]->w;
    *height = modes[mode_index]->h;
    *bpp = bits_per_pixel(modes[mode_index]->format);
    SDL_free(modes);
    return true;
}

//==============================================================================
void get_window_size(RenderWindowHandle rw_handle, int* width, int* height)
{
    RenderWindow* rw = (RenderWindow*)rw_handle;
    assert(rw && width && height);
    *width = rw->width_;
    *height = rw->height_;
}

//==============================================================================
void get_drawable_size(RenderWindowHandle rw_handle, int* width, int* height)
{
    RenderWindow* rw = (RenderWindow*)rw_handle;
    assert(rw && width && height);
	SDL_GetWindowSizeInPixels(rw->window_, width, height);
}

//==============================================================================
void destroy_window(RenderWindowHandle rw_handle)
{
    RenderWindow* rw = (RenderWindow*)rw_handle;
    SDL_DestroyWindow(rw->window_);
    delete rw;

    g_sdl_window = NULL;
}

}; // namespace graphics
