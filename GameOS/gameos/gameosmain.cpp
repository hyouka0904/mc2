#include "gameos.hpp"
#include "gos_render.h"
#include <stdio.h>
#include <time.h>

#include <SDL3/SDL.h>
#include "gos_input.h"

#include "utils/camera.h"
#include "render/render_device.h"
#include "utils/timing.h"
#include "utils/run_options.h"
#include "utils/png_writer.h"
#include "utils/input_script.h"
#include "platform_clock.h"

#include <vector>
#include <chrono>
#include <filesystem>
#include <system_error>

#include <signal.h>

extern bool gos_CreateRenderer(graphics::RenderContextHandle ctx_h, graphics::RenderWindowHandle win_h, int w, int h);
extern void gos_DestroyRenderer();
extern void gos_RendererBeginFrame();
extern void gos_RendererEndFrame();
extern void gos_RendererHandleEvents();
extern void gos_RenderUpdateDebugInput();
extern void gos_RenderEnableDebugDrawCalls();
extern bool gos_RenderGetEnableDebugDrawCalls();

extern bool gosExitGameOS();
extern void gos_SetForceWindowed(bool windowed);
extern void gos_SetShaderHotReload(bool enable);
extern void gos_SetDrawBatching(bool enable);
extern void gos_SetGPUShapes(bool enable);

#include "gameos_sound.h"
#include "mods/gos_mods.h"
#include "mc2_version.h"

static bool g_exit = false;
#if 0
static camera g_camera;
#endif

static void handle_key_down( const SDL_KeyboardEvent& key ) {
    switch( key.key ) {
        case SDLK_ESCAPE:
            if(key.mod & SDL_KMOD_RALT)
                g_exit = true;
            break;
        case SDLK_D:
            if(key.mod & SDL_KMOD_RALT)
                gos_RenderEnableDebugDrawCalls();
            break;
    }
}

static void process_events( void ) {

    input::beginUpdateMouseState();

    SDL_Event event;
    while( SDL_PollEvent( &event ) ) {
        switch( event.type ) {
        case SDL_EVENT_KEY_DOWN:
            handle_key_down( event.key );
            // fallthrough
        case SDL_EVENT_KEY_UP:
            input::handleKeyEvent(&event);
            break;
        case SDL_EVENT_QUIT:
            g_exit = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            // mouse coordinates are normalised by the drawable size
            Environment.drawableWidth = event.window.data1;
            Environment.drawableHeight = event.window.data2;
            gos_render::device()->setViewport(0, 0, event.window.data1, event.window.data2);
            SPEW(("INPUT", "resize event: w: %d h:%d\n", event.window.data1, event.window.data2));
            break;
        case SDL_EVENT_MOUSE_MOTION:
            input::handleMouseMotion(&event);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input::handleMouseWheel(&event);
            break;
        }
    }

    input::updateMouseState();
    input::updateKeyboardState();
}

extern bool g_disable_quads;

static void draw_screen( void )
{
    g_disable_quads = false;
    gos_render::Device* dev = gos_render::device();
    dev->setViewport(0, 0, Environment.drawableWidth, Environment.drawableHeight);

    // Render states deliberately persist across frames (GameOS semantics the
    // game code relies on); clear() forces depth writes on so the depth
    // buffer is cleared every frame.
    dev->clear(true, true);

    gos_RendererBeginFrame();
    Environment.UpdateRenderers();
    gos_RendererEndFrame();

    dev->endShader();
}

extern float frameRate;


static void apply_script_event(const input_script::Event& e)
{
    switch (e.type) {
    case input_script::Type::Move:
        input::scriptedMouseMove(e.x, e.y);
        break;
    case input_script::Type::ButtonDown:
    case input_script::Type::ButtonUp:
        input::scriptedMouseButton(e.button, e.type == input_script::Type::ButtonDown);
        break;
    case input_script::Type::KeyDown:
    case input_script::Type::KeyUp: {
        SDL_Scancode sc = SDL_GetScancodeFromName(e.key.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN)
            fprintf(stderr, "input script: unknown key '%s'\n", e.key.c_str());
        else
            input::scriptedKey(sc, e.type == input_script::Type::KeyDown);
        break;
    }
    }
}

// Reads the back buffer of the frame just drawn and saves it as PNG.
static bool capture_frame(graphics::RenderWindowHandle win, const gos_run_options::Options& opts, uint32_t frame)
{
    int w = 0, h = 0;
    graphics::get_drawable_size(win, &w, &h);
    if (w <= 0 || h <= 0)
        return false;
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    if (!gos_render::device()->readPixels(w, h, pixels.data()))
        return false;
    const std::string path = opts.capture_dir + "/" + gos_run_options::capture_file_name(frame);
    const bool ok = png_writer::write_file(path, pixels.data(), w, h, w * 4, true);
    fprintf(stderr, "CAPTURE: frame %u -> %s%s\n", frame, path.c_str(), ok ? "" : " (FAILED)");
    return ok;
}

#ifndef PLATFORM_WINDOWS
// MessageBox handler that asks the user with an SDL dialog. Falls back to the
// automatic answer when the dialog cannot be shown.
static int sdl_message_box(const char* caption, const char* message, UINT type)
{
    fprintf(stderr, "MSGBOX: %s : %s\n", caption, message);

    struct Button { int id; const char* text; };
    static const Button ok[] = {{IDOK, "OK"}};
    static const Button ok_cancel[] = {{IDOK, "OK"}, {IDCANCEL, "Cancel"}};
    static const Button abort_retry_ignore[] = {{IDABORT, "Abort"}, {IDRETRY, "Retry"}, {IDIGNORE, "Ignore"}};
    static const Button yes_no_cancel[] = {{IDYES, "Yes"}, {IDNO, "No"}, {IDCANCEL, "Cancel"}};
    static const Button yes_no[] = {{IDYES, "Yes"}, {IDNO, "No"}};
    static const Button retry_cancel[] = {{IDRETRY, "Retry"}, {IDCANCEL, "Cancel"}};
    const Button* buttons = ok;
    int num_buttons = 1;
#define MC2_BUTTONS(a) (buttons = a, num_buttons = (int)(sizeof(a) / sizeof(a[0])))
    switch (type & MB_TYPEMASK) {
        case MB_OKCANCEL: MC2_BUTTONS(ok_cancel); break;
        case MB_ABORTRETRYIGNORE: MC2_BUTTONS(abort_retry_ignore); break;
        case MB_YESNOCANCEL: MC2_BUTTONS(yes_no_cancel); break;
        case MB_YESNO: MC2_BUTTONS(yes_no); break;
        case MB_RETRYCANCEL: MC2_BUTTONS(retry_cancel); break;
        default: break;
    }
#undef MC2_BUTTONS
    const int fallback = platform_auto_answer_message_box(type);

    SDL_MessageBoxButtonData data[3];
    for (int i = 0; i < num_buttons; ++i) {
        const Button& b = buttons[i];
        SDL_MessageBoxButtonData& d = data[i];
        d = SDL_MessageBoxButtonData();
        d.buttonID = b.id;
        d.text = b.text;
        if (i == 0)
            d.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (b.id == fallback)
            d.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }

    SDL_MessageBoxData box = {};
    switch (type & MB_ICONMASK) {
        case MB_ICONERROR: box.flags = SDL_MESSAGEBOX_ERROR; break;
        case MB_ICONWARNING: box.flags = SDL_MESSAGEBOX_WARNING; break;
        default: box.flags = SDL_MESSAGEBOX_INFORMATION; break;
    }
    extern SDL_Window* g_sdl_window;
    box.window = g_sdl_window;
    box.title = caption;
    box.message = message;
    box.numbuttons = num_buttons;
    box.buttons = data;

    int pressed = -1;
    if (!SDL_ShowMessageBox(&box, &pressed) || pressed < 0)
        return fallback;
    return pressed;
}
#endif

// The game opens its data relative to the current directory. When it is
// started from a file manager (Finder, Explorer) the current directory is
// somewhere else, so look for the game data next to the executable, in
// ../share/mechcommander2 (Linux packages: bin/mc2 + share/mechcommander2/),
// in the .app bundle's resources, or next to the .app bundle.
static void find_game_directory(const std::string& data_path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!data_path.empty()) {
        fs::current_path(data_path, ec);
        if (ec)
            fprintf(stderr, "-datapath %s: %s\n", data_path.c_str(), ec.message().c_str());
        else if (!fs::is_directory("data", ec))
            fprintf(stderr, "-datapath %s: no data directory there\n", data_path.c_str());
        return;
    }
    if (fs::is_directory("data", ec))
        return;
    const char* base = SDL_GetBasePath();
    if (!base)
        return;
    fs::path dir = fs::path(base);
    if (dir.filename().empty())
        dir = dir.parent_path(); // trailing separator
    std::vector<fs::path> candidates = {dir, dir.parent_path() / "share" / "mechcommander2"};
    // .app bundle: <folder>/MC2.app/Contents/Resources
    if (dir.filename() == "Resources" && dir.parent_path().filename() == "Contents")
        candidates.push_back(dir.parent_path().parent_path().parent_path());
    for (const fs::path& c : candidates) {
        if (fs::is_directory(c / "data", ec)) {
            fs::current_path(c, ec);
            if (!ec)
                fprintf(stderr, "Game directory: %s\n", c.string().c_str());
            return;
        }
    }
}

#ifdef _WIN32
void gos_AttachParentConsole(); // win_console.cpp
#endif

#ifndef DISABLE_GAMEOS_MAIN
int main(int argc, char** argv)
{
#ifdef _WIN32
    gos_AttachParentConsole();
#endif
    gos_run_options::Options run_opts;
    {
        std::string error;
        if (!gos_run_options::parse(argc, argv, run_opts, &error)) {
            fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
    }
    if (run_opts.print_version) {
        printf("MechCommander 2 %s\n", MC2_VERSION);
        return 0;
    }
    fprintf(stderr, "MechCommander 2 %s\n", MC2_VERSION);
    find_game_directory(run_opts.data_path);
    // Before the game initialises: it seeds and reads the clock during startup
    platform_clock::set_fixed_step(run_opts.fixed_step_us);
#ifndef PLATFORM_WINDOWS
    // Real dialogs only when someone can answer them: not in deterministic or
    // scripted runs, and not when MC2_NO_DIALOGS is set (CI, test scripts).
    if (!run_opts.fixed_step_us && run_opts.input_script.empty() && !SDL_getenv("MC2_NO_DIALOGS"))
        platform_set_message_box_handler(sdl_message_box);
#endif
    if (run_opts.has_seed)
        gos_SetFixedRandomSeed(run_opts.seed);
    gos_SetForceWindowed(run_opts.windowed);
    gos_SetShaderHotReload(run_opts.shader_reload);
    gos_SetDrawBatching(run_opts.batching);
    gos_SetGPUShapes(run_opts.gpu_shapes);
    // SDL3 GPU unless -renderer says otherwise; OpenGL is the fallback
#ifndef MC2_NO_OPENGL
    gos_render::registerGLBackend();
#endif
    const bool explicit_renderer = !run_opts.renderer.empty();
    {
        gos_render::Backend b = gos_render::Backend::GPU;
        if (explicit_renderer)
            gos_render::parseBackend(run_opts.renderer.c_str(), &b);
        gos_render::setBackend(b);
    }

    // Mods must be active before the game opens its first data file
    {
        std::string report;
        const bool ok = gos_ModsInit(run_opts.mods_dir.empty() ? NULL : run_opts.mods_dir.c_str(),
                                     run_opts.modpack.empty() ? NULL : run_opts.modpack.c_str(), &report);
        if (gos_ModsActive() || !ok)
            fprintf(stderr, "MODS: %s", report.c_str());
        if (!ok) {
            fprintf(stderr, "error: the requested mods cannot be loaded (see above)\n");
            return 3;
        }
    }

    std::vector<input_script::Event> script_events;
    if (!run_opts.input_script.empty()) {
        std::string error;
        if (!input_script::load_file(run_opts.input_script, script_events, &error)) {
            fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
        input::setScriptedInput(true);
    }
    input_script::Player script_player(std::move(script_events));

    FILE* perf_csv = NULL;
    if (!run_opts.perf_csv.empty()) {
        perf_csv = fopen(run_opts.perf_csv.c_str(), "w");
        if (!perf_csv) {
            fprintf(stderr, "error: cannot write %s\n", run_opts.perf_csv.c_str());
            return 2;
        }
        fprintf(perf_csv, "frame,frame_ms,draw_calls,pipeline_changes,pipelines,cpu_ms\n");
    }

    //signal(SIGTRAP, SIG_IGN);

    // gather command line
	size_t cmdline_len = 0;
    for(int i=0;i<argc;++i) {
        cmdline_len += strlen(argv[i]);
        cmdline_len += 1; // ' '
    }
    char* cmdline = new char[cmdline_len + 1];
    size_t offset = 0;
    for(int i=0;i<argc;++i) {
        size_t arglen = strlen(argv[i]);
        memcpy(cmdline + offset, argv[i], arglen);
        cmdline[offset + arglen] = ' ';
        offset += arglen + 1;
    }
    cmdline[cmdline_len] = '\0';

    // fills in Environment structure
    GetGameOSEnvironment(cmdline);

    delete[] cmdline;
    cmdline = NULL;

    int w = Environment.screenWidth;
    int h = Environment.screenHeight;

    graphics::RenderWindowHandle win = graphics::create_window("MechCommander 2 " MC2_VERSION, w, h);
    if(!win)
        return 1;

    graphics::RenderContextHandle ctx = graphics::init_render_context(win);
    if(!ctx)
        return 1;

    graphics::make_current_context(ctx);

    if (!gos_CreateRenderer(ctx, win, w, h)) {
        if (explicit_renderer || gos_render::backend() != gos_render::Backend::GPU)
            return 1;
        // e.g. no Vulkan driver: the window must be recreated for OpenGL
        fprintf(stderr, "SDL3 GPU renderer unavailable: falling back to OpenGL\n");
        graphics::destroy_render_context(ctx);
        graphics::destroy_window(win);
        gos_render::setBackend(gos_render::Backend::GL);
        win = graphics::create_window("MechCommander 2 " MC2_VERSION, w, h);
        ctx = win ? graphics::init_render_context(win) : NULL;
        if (!ctx)
            return 1;
        graphics::make_current_context(ctx);
        if (!gos_CreateRenderer(ctx, win, w, h))
            return 1;
    }
    if(!gos_CreateAudio())
    {   // not an error
        SPEW(("AUDIO", "Failed to create audio\n"));
    }

    Environment.InitializeGameEngine();

#if 0
	float aspect = (float)w/(float)h;
	mat4 proj_mat = frustumProjMatrix(-aspect*0.5f, aspect*0.5f, -.5f, .5f, 1.0f, 100.0f);
	g_camera.set_projection(proj_mat);
	g_camera.set_view(mat4::translation(vec3(0, 0, -16)));
#endif

	timing::init();

    uint32_t frame_number = 0;
    while( !g_exit ) {

		uint64_t start_tick = timing::gettickcount();
		const auto perf_start = std::chrono::steady_clock::now();
		const double perf_cpu_start = perf_csv ? timing::thread_cpu_ms() : 0.0;
		++frame_number;
		if (platform_clock::is_fixed()) {
			// Deterministic mode: the game sees a constant frame time no
			// matter how long the frame really took, and nothing waits.
			frameRate = 1e6f / (float)platform_clock::fixed_step_us();
		} else {
			timing::sleep(10*1000000);
		}

        for (const input_script::Event& e : script_player.take(frame_number))
            apply_script_event(e);

        if(gos_RenderGetEnableDebugDrawCalls()) {
            gos_RenderUpdateDebugInput();
        } else {
            Environment.DoGameLogic();
        }

        process_events();

		gos_RendererHandleEvents();

        graphics::make_current_context(ctx);
        draw_screen();
        if (run_opts.should_capture(frame_number))
            capture_frame(win, run_opts, frame_number);
        graphics::swap_window(win);

        if (perf_csv) {
            const gos_render::Device::Stats st = gos_render::device()->stats();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perf_start).count();
            fprintf(perf_csv, "%u,%.3f,%u,%u,%u,%.3f\n", frame_number, ms, st.draw_calls,
                    st.pipeline_changes, st.pipeline_cache_size, timing::thread_cpu_ms() - perf_cpu_start);
        }

        g_exit |= gosExitGameOS();
        if (run_opts.exit_after_frame && frame_number >= run_opts.exit_after_frame)
            g_exit = true;

        if (platform_clock::is_fixed()) {
            platform_clock::advance_frame();
        } else {
            uint64_t end_tick = timing::gettickcount();
            uint64_t dt = timing::ticks2ms(end_tick - start_tick);
            frameRate = 1000.0f / (float)(dt ? dt : 1);
        }
    }
    
    if (perf_csv)
        fclose(perf_csv);

    Environment.TerminateGameEngine();

    gos_DestroyRenderer();

    graphics::destroy_render_context(ctx);
    graphics::destroy_window(win);

    gos_DestroyAudio();
    gos_ModsShutdown();

    return 0;
}
#endif // DISABLE_GAMEOS_MAIN
