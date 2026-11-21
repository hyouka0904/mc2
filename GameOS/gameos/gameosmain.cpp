#include "gameos.hpp"
#include "gos_render.h"
#include <stdio.h>
#include <time.h>

#include <SDL2/SDL.h>
#include "gos_input.h"

#include "utils/camera.h"
#include "utils/shader_builder.h"
#include "utils/gl_utils.h"
#include "utils/timing.h"
#include "utils/run_options.h"
#include "utils/png_writer.h"
#include "utils/input_script.h"
#include "platform_clock.h"

#include <vector>

#include <signal.h>

extern void gos_CreateRenderer(graphics::RenderContextHandle ctx_h, graphics::RenderWindowHandle win_h, int w, int h);
extern void gos_DestroyRenderer();
extern void gos_RendererBeginFrame();
extern void gos_RendererEndFrame();
extern void gos_RendererHandleEvents();
extern void gos_RenderUpdateDebugInput();
extern void gos_RenderEnableDebugDrawCalls();
extern bool gos_RenderGetEnableDebugDrawCalls();

extern bool gosExitGameOS();

extern bool gos_CreateAudio();
extern void gos_DestroyAudio();

static bool g_exit = false;
static bool g_focus_lost = false;
#if 0
static camera g_camera;
#endif

static void handle_key_down( SDL_Keysym* keysym ) {
    switch( keysym->sym ) {
        case SDLK_ESCAPE:
            if(keysym->mod & KMOD_RALT)
                g_exit = true;
            break;
        case 'd':
            if(keysym->mod & KMOD_RALT)
                gos_RenderEnableDebugDrawCalls();
            break;
    }
}

static void process_events( void ) {

    input::beginUpdateMouseState();

    SDL_Event event;
    while( SDL_PollEvent( &event ) ) {

        if(g_focus_lost) {
            if(event.type != SDL_WINDOWEVENT_FOCUS_GAINED) {
                continue;
            } else {
                g_focus_lost = false;
            }
        }

        switch( event.type ) {
        case SDL_KEYDOWN:
            handle_key_down( &event.key.keysym );
            // fallthrough
        case SDL_KEYUP:
            input::handleKeyEvent(&event);
            break;
        case SDL_QUIT:
            g_exit = true;
            break;
		case SDL_WINDOWEVENT_RESIZED:
			{
				float w = (float)event.window.data1;
				float h = (float)event.window.data2;
				glViewport(0, 0, (GLsizei)w, (GLsizei)h);
                SPEW(("INPUT", "resize event: w: %f h:%f\n", w, h));
			}
			break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            g_focus_lost = true;
            break;
        case SDL_MOUSEMOTION:
            input::handleMouseMotion(&event); 
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            //input::handleMouseButton(&event);
            break;
        case SDL_MOUSEWHEEL:
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
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glCullFace(GL_FRONT);
    //CHECK_GL_ERROR;
    
	const int viewport_w = Environment.drawableWidth;
	const int viewport_h = Environment.drawableHeight;
    glViewport(0, 0, viewport_w, viewport_h);
#if 0
    mat4 proj;
    g_camera.get_projection(&proj);
    mat4 viewM;
    g_camera.get_view(&viewM);
#endif

#if 0
    gos_VERTEX q[4];
    q[0].x = 10; q[0].y = 10;
    q[0].z = 0.0;
    q[0].rhw = 1;
    q[0].argb = 0xffff0000;
    q[1] = q[2] = q[3] = q[0];

    q[1].x = 210; q[1].y = 10;
    q[2].x = 110; q[2].y = 210;
    q[3].x = 10; q[3].y = 210;

    g_disable_quads = false;
    gos_DrawQuads(&q[0], 4);
    g_disable_quads = true;
#endif
	/*
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadTransposeMatrixf((const float*)proj);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadTransposeMatrixf((const float*)viewM);
	*/

    CHECK_GL_ERROR;

    // Render states deliberately persist across frames (GameOS semantics the
    // game code relies on). Only the depth write mask is forced, because
    // glClear() honours it and the depth buffer must be cleared every frame.
    glDepthMask(GL_TRUE);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    gos_RendererBeginFrame();
    Environment.UpdateRenderers();
    gos_RendererEndFrame();

    glUseProgram(0);
    //CHECK_GL_ERROR;
}

extern float frameRate;


const char* getStringForType(GLenum type)
{
	switch (type)
	{
	case GL_DEBUG_TYPE_ERROR: return "DEBUG_TYPE_ERROR";
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEBUG_TYPE_DEPRECATED_BEHAVIOR";
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "DEBUG_TYPE_UNDEFINED_BEHAVIOR";
	case GL_DEBUG_TYPE_PERFORMANCE: return "DEBUG_TYPE_PERFORMANCE";
	case GL_DEBUG_TYPE_PORTABILITY: return "DEBUG_TYPE_PORTABILITY";
	case GL_DEBUG_TYPE_MARKER: return "DEBUG_TYPE_MARKER";
	case GL_DEBUG_TYPE_PUSH_GROUP: return "DEBUG_TYPE_PUSH_GROUP";
	case GL_DEBUG_TYPE_POP_GROUP: return "DEBUG_TYPE_POP_GROUP";
	case GL_DEBUG_TYPE_OTHER: return "DEBUG_TYPE_OTHER";
	default: return "(undefined)";
	}
}

const char* getStringForSource(GLenum type)
{
	switch (type)
	{
	case GL_DEBUG_SOURCE_API: return "DEBUG_SOURCE_API";
	case GL_DEBUG_SOURCE_SHADER_COMPILER: return "DEBUG_SOURCE_SHADER_COMPILER";
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "DEBUG_SOURCE_WINDOW_SYSTEM";
	case GL_DEBUG_SOURCE_THIRD_PARTY: return "DEBUG_SOURCE_THIRD_PARTY";
	case GL_DEBUG_SOURCE_APPLICATION: return "DEBUG_SOURCE_APPLICATION";
	case GL_DEBUG_SOURCE_OTHER: return "DEBUG_SOURCE_OTHER";
	default: return "(undefined)";
	}
}

const char* getStringForSeverity(GLenum type)
{
	switch (type)
	{
	case GL_DEBUG_SEVERITY_HIGH: return "DEBUG_SEVERITY_HIGH";
	case GL_DEBUG_SEVERITY_MEDIUM: return "DEBUG_SEVERITY_MEDIUM";
	case GL_DEBUG_SEVERITY_LOW: return "DEBUG_SEVERITY_LOW";
	case GL_DEBUG_SEVERITY_NOTIFICATION: return "DEBUG_SEVERITY_NOTIFICATION";
	default: return "(undefined)";
	}
}
//typedef void (GLAPIENTRY *GLDEBUGPROCARB)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
#ifdef PLATFORM_WINDOWS
void GLAPIENTRY OpenGLDebugLog(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
#else
void GLAPIENTRY OpenGLDebugLog(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, GLvoid* userParam)
#endif
{
	if (severity != GL_DEBUG_SEVERITY_NOTIFICATION && severity != GL_DEBUG_SEVERITY_LOW)
	{
		printf("Type: %s; Source: %s; ID: %d; Severity : %s\n",
			getStringForType(type),
			getStringForSource(source),
			id,
			getStringForSeverity(severity)
		);
		printf("Message : %s\n", message);
	}
}

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
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::string path = opts.capture_dir + "/" + gos_run_options::capture_file_name(frame);
    const bool ok = png_writer::write_file(path, pixels.data(), w, h, w * 4, true);
    fprintf(stderr, "CAPTURE: frame %u -> %s%s\n", frame, path.c_str(), ok ? "" : " (FAILED)");
    return ok;
}

#ifndef DISABLE_GAMEOS_MAIN
int main(int argc, char** argv)
{
    gos_run_options::Options run_opts;
    {
        std::string error;
        if (!gos_run_options::parse(argc, argv, run_opts, &error)) {
            fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
    }
    // Before the game initialises: it seeds and reads the clock during startup
    platform_clock::set_fixed_step(run_opts.fixed_step_us);
    if (run_opts.has_seed)
        gos_SetFixedRandomSeed(run_opts.seed);

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

    graphics::RenderWindowHandle win = graphics::create_window("mc2", w, h);
    if(!win)
        return 1;

    graphics::RenderContextHandle ctx = graphics::init_render_context(win);
    if(!ctx)
        return 1;

    graphics::make_current_context(ctx);

    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        SPEW(("GLEW", "Error: %s\n", glewGetErrorString(err)));
        return 1;
    }
    // glewInit() probes extensions with glGetString(GL_EXTENSIONS), which
    // raises GL_INVALID_ENUM on core profile contexts. Drain that error so it
    // is not reported later by an unrelated CHECK_GL_ERROR.
    while (glGetError() != GL_NO_ERROR) {}

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	//glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 76, 1, "My debug group");
	glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
	glDebugMessageCallbackARB((GLDEBUGPROC)&OpenGLDebugLog, NULL);


    SPEW(("GRAPHICS", "Status: Using GLEW %s\n", glewGetString(GLEW_VERSION)));
    //if ((!GLEW_ARB_vertex_program || !GLEW_ARB_fragment_program))
    //{
     //   SPEW(("GRAPHICS", "No shader program support\n"));
      //  return 1;
    //}

    if(!glewIsSupported("GL_VERSION_3_0")) {
        SPEW(("GRAPHICS", "Minimum required OpenGL version is 3.0\n"));
        return 1;
    }

    const char* glsl_version = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    SPEW(("GRAPHICS", "GLSL version supported: %s\n", glsl_version));

    int glsl_maj = 0, glsl_min = 0;
    sscanf(glsl_version, "%d.%d", &glsl_maj, &glsl_min);

    if(glsl_maj < 3 || (glsl_maj==3 && glsl_min < 30) ) {
        SPEW(("GRAPHICS", "Minimum required OpenGL version is 330 ES, current: %d.%d\n", glsl_maj, glsl_min));
        return 1;
    }

    char version[16] = {0};
    snprintf(version, sizeof(version), "%d%d", glsl_maj, glsl_min);
    SPEW(("GRAPHICS", "Using %s shader version\n", version));

    gos_CreateRenderer(ctx, win, w, h);
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
    
    Environment.TerminateGameEngine();

    gos_DestroyRenderer();

    graphics::destroy_render_context(ctx);
    graphics::destroy_window(win);

    gos_DestroyAudio();

    return 0;
}
#endif // DISABLE_GAMEOS_MAIN
