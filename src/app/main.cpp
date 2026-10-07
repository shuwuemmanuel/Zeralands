// ZeraLands editor entry point.
#include "app/App.h"
#include "core/ImageIO.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static void glfwError(int code, const char* msg) { std::fprintf(stderr, "GLFW %d: %s\n", code, msg); }

int main(int argc, char** argv) {
    // automation (CI screenshots / smoke tests)
    std::string screenshot, env, era, library, prompt;
    int view = 0, tool = 0, frames = 40, res = 0, tab = -1;
    bool generate = false, rail = false, race = false, camSet = false;
    float camv[5] = {0, 0, 0, 0, 0};
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--screenshot") screenshot = next();
        else if (a == "--env") env = next();
        else if (a == "--era") era = next();
        else if (a == "--library") library = next();
        else if (a == "--prompt") prompt = next();
        else if (a == "--view") view = next() == "2d" ? 1 : 0;
        else if (a == "--tool") tool = std::stoi(next());
        else if (a == "--tab") tab = std::stoi(next());
        else if (a == "--frames") frames = std::stoi(next());
        else if (a == "--res") res = std::stoi(next());
        else if (a == "--generate") generate = true;
        else if (a == "--camera") {   // x,y,z,yawDeg,pitchDeg (fractions of world size for x/z, meters for y)
            std::string v = next();
            std::sscanf(v.c_str(), "%f,%f,%f,%f,%f", &camv[0], &camv[1], &camv[2], &camv[3], &camv[4]);
            camSet = true;
        }
        else if (a == "--rail") rail = true;
        else if (a == "--race") race = true;
    }

    glfwSetErrorCallback(glfwError);
#if defined(__linux__) && defined(GLFW_PLATFORM_X11)
    // GLEW resolves entry points through GLX; prefer X11 (XWayland on Wayland desktops).
    if (glfwPlatformSupported(GLFW_PLATFORM_X11)) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_MAXIMIZED, screenshot.empty() ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1600, 940, "ZeraLands 2.0", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Could not create an OpenGL 3.3 window.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "GLEW init failed\n");
        return 1;
    }
    glGetError();   // GLEW may leave a benign error on core profiles

    std::string exeDir = fs::absolute(fs::u8path(argv[0])).parent_path().u8string();
    {
        zl::Image8 icon;
        for (const auto& p : {fs::u8path(exeDir) / "assets" / "icon.png", fs::current_path() / "assets" / "icon.png"})
            if (zl::loadImage8(p.u8string(), icon, 4)) {
                GLFWimage gi{icon.w, icon.h, icon.px.data()};
                glfwSetWindowIcon(window, 1, &gi);
                break;
            }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    zl::App app;
    std::string err;
    if (!app.init(window, exeDir, err)) {
        std::fprintf(stderr, "Renderer init failed:\n%s\n", err.c_str());
        return 1;
    }
    if (!env.empty()) app.settings().envIndex = std::max(0, zl::findEnvironment(env));
    if (!era.empty()) app.settings().eraIndex = std::max(0, zl::findEra(era));
    if (!prompt.empty()) app.settings().prompt = prompt;
    if (res > 0) app.settings().resolution = res;
    if (rail) app.settings().rail = true;
    if (race) app.settings().racetrack = true;
    if (!library.empty()) {
        app.libraryPath() = library;
        app.scanLibrary();
    }
    app.setView(view);
    app.setTool(tool);
    if (tab >= 0) app.setTab(tab);
    if (generate) app.startGenerate();

    auto last = std::chrono::steady_clock::now();
    int frame = 0, settled = 0;
    while (!glfwWindowShouldClose(window) && !app.quitRequested()) {
        glfwPollEvents();
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.frame(std::min(dt, 0.1f));
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.07f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!screenshot.empty()) {
            ++frame;
            bool ready = !app.busy() && (!generate || app.scene().valid());
            settled = ready ? settled + 1 : 0;
            if (camSet && settled == 2 && app.scene().valid()) {
                float W = app.scene().terrain.worldSize;
                auto& c = app.camera();
                c.pos = zl::Vec3(camv[0] * W, app.scene().terrain.heightAtWorld(camv[0] * W, camv[2] * W) + camv[1], camv[2] * W);
                c.yaw = camv[3] / 57.2958f;
                c.pitch = camv[4] / 57.2958f;
            }
            if (settled > frames) {
                std::vector<uint8_t> px(size_t(w) * h * 4), flip(px.size());
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                for (int y = 0; y < h; ++y) std::memcpy(&flip[size_t(y) * w * 4], &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
                zl::savePng8(screenshot, w, h, 4, flip.data());
                std::printf("screenshot %s (%dx%d) after %d frames\n", screenshot.c_str(), w, h, frame);
                break;
            }
        }
        glfwSwapBuffers(window);
    }
    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
