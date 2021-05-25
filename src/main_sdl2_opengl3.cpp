// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan graphics context creation, etc.)
// (GL3W is a helper library to access OpenGL functions since there is no standard header to access modern OpenGL functions easily. Alternatives are GLEW, Glad, etc.)

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL.h>
#include "sclient.h"
#include "rspice.h"
#include "res.h"
#include "ketopt.h"
#include <string>

// About Desktop OpenGL function loaders:
//  Modern desktop OpenGL doesn't have a standard portable header file to load OpenGL function pointers.
//  Helper libraries are often used for this purpose! Here we are supporting a few common ones (gl3w, glew, glad).
//  You may use another loader/header of your choice (glext, glLoadGen, etc.), or chose to manually implement your own.
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h>    // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h>    // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h>  // Initialize with gladLoadGL()
#else
#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

// Main code
int main(int argc, char*argv[])
{
    static ko_longopt_t longopts[] = {
		{ "fullscreen", ko_no_argument, 301 },
		{ "spice", ko_required_argument, 302 },
		{ "kiosk", ko_no_argument, 303 },
		{ "host", ko_required_argument, 304 },
		{ "port", ko_required_argument, 305 },
		{ "title", ko_required_argument, 306 },
		{ NULL, 0, 0 }
	};

    ketopt_t opt = KETOPT_INIT;
    int c;
    bool full_screen = false, kiosk = false;
    std::string host, port, title;
	while ((c = ketopt(&opt, argc, argv, 0, "", longopts)) >= 0)
    {

        switch (c)
        {
        case 301:
            full_screen = true;
            break;
        case 302:
            if (opt.arg)
                rspice_cache_args(opt.arg);
            break;
        case 303:
            kiosk = full_screen = true;
            break;
        case 304:
            host = opt.arg ? opt.arg : "";
            break;
        case 305:
            port = opt.arg ? opt.arg : "";
            break;
        case 306:
            title = opt.arg ? opt.arg : "";
            break;
        default:
            break;
        }
    }

    // Setup SDL
    // (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
    // depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to latest version of SDL is recommended!)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#if __APPLE__
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window;
    int window_w = 1024, window_h = 768;
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI );
    if (full_screen)
    {
        SDL_DisplayMode cur_display_mode;
        SDL_GetDesktopDisplayMode(0, &cur_display_mode);
        window_w = cur_display_mode.w;
        window_h = cur_display_mode.h;
        /* TODO 有以下两个问题
       1. 在mingw 环境加SDL_WINDOW_FULLSCREEN 可以减少GPU的使用率, 原因未查
       2. 在buildroot 环境下SDL_WINDOW_FULLSCREEN不能输入, 原因未查, 暂使用SDL_WINDOW_BORDERLESS 规避 */
#ifdef _SDL_WIND_BOARDLESS
        window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
#else
        window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
    }
    window = SDL_CreateWindow(title.empty() ? "sclient" : title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_w, window_h, window_flags);
    sclient_set_paintinfo(window, window_w, window_h);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");
    // Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    bool err = gladLoadGL() == 0;
#else
    bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = NULL;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsLight();
    //ImGui::StyleColorsClassic();
    ImGuiStyle &style = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.43f, 0.87f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.43f, 0.87f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);

    // Setup Platform/Renderer bindings
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(res_Roboto_Medium_ttf, res_Roboto_Medium_ttf_len, 16.0f, &font_cfg); // The font size affects widgets size too
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
    unsigned SCREEN_TICK_PER_FRAME = 1000 / 60;

    sclient_set_misc(kiosk, host, port);
    // Main loop
    bool done = false;
    while (!done)
    {
        uint32_t frame_start = SDL_GetTicks();
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type)
            {
            case SDL_QUIT:
                done = true;
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event)
                {
                case SDL_WINDOWEVENT_HIDDEN:
                case SDL_WINDOWEVENT_MINIMIZED:
                    SCREEN_TICK_PER_FRAME = 1000 / 1;
                    break;
                case SDL_WINDOWEVENT_MAXIMIZED:
                case SDL_WINDOWEVENT_RESTORED:
                    SCREEN_TICK_PER_FRAME = 1000 / 60;
#if defined(__linux__)
                    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN))
                    {
                        SDL_SetWindowFullscreen(window, 0);
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
#endif
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    sclient_set_paintinfo(window, event.window.data1, event.window.data2);
                    break;
                case SDL_WINDOWEVENT_CLOSE:
                    done = (event.window.windowID == SDL_GetWindowID(window));
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        sclient_paint_one_frame();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        uint32_t frame_diff = SDL_GetTicks() - frame_start;
        if(frame_diff < SCREEN_TICK_PER_FRAME / 2 )
            SDL_Delay(( SCREEN_TICK_PER_FRAME - frame_diff));
    }

    // Cleanup
    sclient_cleanup();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    rspice_cache_free();
    return 0;
}
