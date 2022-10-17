#include <argparser.h>
#include <dlfcn.h>
#include <game_window_manager.h>
#include <log.h>
#include <mcpelauncher/crash_handler.h>
#include <mcpelauncher/minecraft_utils.h>
#include <mcpelauncher/minecraft_version.h>
#include <mcpelauncher/mod_loader.h>
#include <mcpelauncher/path_helper.h>
#include "gl_core_patch.h"
#include "hbui_patch.h"
#include "jni/jni_support.h"
#include "jni/store.h"
#include "shader_error_patch.h"
#include "splitscreen_patch.h"
#include "window_callbacks.h"
#include "xbox_live_helper.h"
#if defined(__i386__) || defined(__x86_64__)
#include "cpuid.h"
#include "texel_aa_patch.h"
#include "xbox_shutdown_patch.h"
#endif
#include <build_info.h>
#include <libc_shim.h>
#include <mcpelauncher/linker.h>
#include <mcpelauncher/patch_utils.h>
#include <minecraft/imported/android_symbols.h>
#include "core_patches.h"
#include "fake_assetmanager.h"
#include "fake_egl.h"
#include "fake_looper.h"
#include "fake_window.h"
#include "main.h"
#include "symbols.h"
#include "thread_mover.h"
// For getpid
#include <unistd.h>

static size_t base;
LauncherOptions options;

void printVersionInfo();

int main(int argc, char* argv[]) {
    auto windowManager = GameWindowManager::getManager();
    CrashHandler::registerCrashHandler();
    MinecraftUtils::workaroundLocaleBug();

    argparser::arg_parser p;
    argparser::arg<bool> printVersion(p, "--version", "-v", "Prints version info");
    argparser::arg<std::string> gameDir(p, "--game-dir", "-dg", "Directory with the game and assets");
    argparser::arg<std::string> dataDir(p, "--data-dir", "-dd", "Directory to use for the data");
    argparser::arg<std::string> cacheDir(p, "--cache-dir", "-dc", "Directory to use for cache");
    argparser::arg<int> windowWidth(p, "--width", "-ww", "Window width", 720);
    argparser::arg<int> windowHeight(p, "--height", "-wh", "Window height", 480);
    argparser::arg<bool> disableFmod(p, "--disable-fmod", "-df", "Disables usage of the FMod audio library");
    argparser::arg<bool> forceEgl(p, "--force-opengles", "-fes", "Force creating an OpenGL ES surface instead of using the glcorepatch hack", !GLCorePatch::mustUseDesktopGL());
    argparser::arg<bool> texturePatch(p, "--texture-patch", "-tp", "Rewrite textures of the game for Minecraft 1.16.210 - 1.17.4X", false);

    if(!p.parse(argc, (const char**)argv))
        return 1;
    if(printVersion) {
        printVersionInfo();
        return 0;
    }
    options.windowWidth = windowWidth;
    options.windowHeight = windowHeight;
    options.graphicsApi = forceEgl.get() ? GraphicsApi::OPENGL_ES2 : GraphicsApi::OPENGL;

    FakeEGL::enableTexturePatch = texturePatch.get();
    if(!gameDir.get().empty())
        PathHelper::setGameDir(gameDir);
    if(!dataDir.get().empty())
        PathHelper::setDataDir(dataDir);
    if(!cacheDir.get().empty())
        PathHelper::setCacheDir(cacheDir);

    Log::info("Launcher", "Version: client %s / manifest %s", CLIENT_GIT_COMMIT_HASH, MANIFEST_GIT_COMMIT_HASH);
#if defined(__i386__) || defined(__x86_64__)
    {
        CpuId cpuid;
        Log::info("Launcher", "CPU: %s %s", cpuid.getManufacturer(), cpuid.getBrandString());
        Log::info("Launcher", "CPU supports SSSE3: %s",
                  cpuid.queryFeatureFlag(CpuId::FeatureFlag::SSSE3) ? "YES" : "NO");
    }
#endif

    Log::trace("Launcher", "Loading hybris libraries");
    linker::init();
    // Fix saving to internal storage without write access to /data/*
    // TODO research how this path is constructed
    auto pid = getpid();
    shim::from_android_data_dir = {
        // Minecraft 1.16.210 or older
        "/data/data/com.mojang.minecraftpe",
        // Minecraft 1.16.210 or later, absolute path on linux (source build ubuntu 20.04)
        std::string("/data/data") + PathHelper::getParentDir(PathHelper::getAppDir()) + "/proc/" + std::to_string(pid) + "/cmdline"};
    if(argc >= 1 && argv != nullptr && argv[0] != nullptr && argv[0][0] != '\0') {
        // Minecraft 1.16.210 or later, relative path on linux (source build ubuntu 20.04) or every path AppImage / flatpak
        shim::from_android_data_dir.emplace_back(argv[0][0] == '/' ? std::string("/data/data") + argv[0] : std::string("/data/data/") + argv[0]);
    }
    // Minecraft 1.16.210 or later, macOS
    shim::from_android_data_dir.emplace_back("/data/data");
    shim::to_android_data_dir = PathHelper::getPrimaryDataDirectory();
    for(auto&& redir : shim::from_android_data_dir) {
        Log::trace("REDIRECT", "%s to %s", redir.data(), shim::to_android_data_dir.data());
    }
    auto libC = MinecraftUtils::getLibCSymbols();
    ThreadMover::hookLibC(libC);

#ifdef USE_ARMHF_SUPPORT
    linker::load_library("ld-android.so", {});
    android_dlextinfo extinfo;
    std::vector<mcpelauncher_hook_t> hooks;
    for(auto&& entry : libC) {
        hooks.emplace_back(mcpelauncher_hook_t{entry.first.data(), entry.second});
    }
    hooks.emplace_back(mcpelauncher_hook_t{nullptr, nullptr});
    extinfo.flags = ANDROID_DLEXT_MCPELAUNCHER_HOOKS;
    extinfo.mcpelauncher_hooks = hooks.data();
    linker::dlopen_ext(PathHelper::findDataFile("lib/" + std::string(PathHelper::getAbiDir()) + "/libc.so").c_str(), 0, &extinfo);
    linker::dlopen(PathHelper::findDataFile("lib/" + std::string(PathHelper::getAbiDir()) + "/libm.so").c_str(), 0);
#else
    linker::load_library("libc.so", libC);
    MinecraftUtils::loadLibM();
#endif
    MinecraftUtils::setupHybris();
    try {
        PathHelper::findGameFile(std::string("lib/") + MinecraftUtils::getLibraryAbi() + "/libminecraftpe.so");
    } catch(std::exception& e) {
        Log::error("LAUNCHER", "Could not find the game, use the -dg flag to fix this error. Original Error: %s", e.what());
        return 1;
    }
    linker::update_LD_LIBRARY_PATH(PathHelper::findGameFile(std::string("lib/") + MinecraftUtils::getLibraryAbi()).data());
    if(!disableFmod) {
        try {
            MinecraftUtils::loadFMod();
        } catch(std::exception& e) {
            Log::warn("FMOD", "Failed to load host libfmod: '%s', use experimental pulseaudio backend if available", e.what());
        }
    }
    FakeEGL::setProcAddrFunction((void* (*)(const char*))windowManager->getProcAddrFunc());
    FakeEGL::installLibrary();
    if(options.graphicsApi == GraphicsApi::OPENGL_ES2) {
        // GLFW needs a window to let eglGetProcAddress return symbols
        FakeLooper::initWindow();
        MinecraftUtils::setupGLES2Symbols(fake_egl::eglGetProcAddress);
    } else {
        // The glcore patch requires an empty library
        // Otherwise linker has to hide the symbols from dlsym in libminecraftpe.so
        linker::load_library("libGLESv2.so", {});
    }

    std::unordered_map<std::string, void*> android_syms;
    FakeAssetManager::initHybrisHooks(android_syms);
    FakeInputQueue::initHybrisHooks(android_syms);
    FakeLooper::initHybrisHooks(android_syms);
    FakeWindow::initHybrisHooks(android_syms);
    for(auto s = android_symbols; *s; s++)  // stub missing symbols
        android_syms.insert({*s, (void*)+[]() { Log::warn("Main", "Android stub called"); }});
    linker::load_library("libandroid.so", android_syms);
    ModLoader modLoader;
    modLoader.loadModsFromDirectory(PathHelper::getPrimaryDataDirectory() + "mods/", true);

    Log::trace("Launcher", "Loading Minecraft library");
    static void* handle = MinecraftUtils::loadMinecraftLib(reinterpret_cast<void*>(&CorePatches::showMousePointer), reinterpret_cast<void*>(&CorePatches::hideMousePointer));
    if(!handle && options.graphicsApi == GraphicsApi::OPENGL) {
        // Old game version or renderdragon
        options.graphicsApi = GraphicsApi::OPENGL_ES2;
        // Unload empty stub library
        auto libGLESv2 = linker::dlopen("libGLESv2.so", 0);
        linker::dlclose(libGLESv2);
        // load fake libGLESv2 library
        // GLFW needs a window to let eglGetProcAddress return symbols
        FakeLooper::initWindow();
        MinecraftUtils::setupGLES2Symbols(fake_egl::eglGetProcAddress);
        // Try load the game again
        handle = MinecraftUtils::loadMinecraftLib(reinterpret_cast<void*>(&CorePatches::showMousePointer), reinterpret_cast<void*>(&CorePatches::hideMousePointer));
    }
    if(!handle) {
        Log::error("Launcher", "Failed to load Minecraft library, please reinstall or wait for an update to support the new release");
        return 51;
    }
    Log::info("Launcher", "Loaded Minecraft library");
    Log::debug("Launcher", "Minecraft is at offset 0x%" PRIXPTR, (uintptr_t)MinecraftUtils::getLibraryBase(handle));
    base = MinecraftUtils::getLibraryBase(handle);

    modLoader.loadModsFromDirectory(PathHelper::getPrimaryDataDirectory() + "mods/");

    Log::info("Launcher", "Game version: %s", MinecraftVersion::getString().c_str());

    Log::info("Launcher", "Applying patches");
    SymbolsHelper::initSymbols(handle);
    CorePatches::install(handle);
#ifdef __i386__
    TexelAAPatch::install(handle);
    HbuiPatch::install(handle);
    SplitscreenPatch::install(handle);
    ShaderErrorPatch::install(handle);
#endif
    // If this Minecraft versions contains this bgfx symbol
    // it is using the renderdragon engine
    if(linker::dlsym(handle, "bgfx_init")) {
        // The directinput mode is incompatible with the most of renderdragon enabled games
        // Hide the availability of these symbols, until the bug is fixed
        Keyboard::_states = nullptr;
        Keyboard::_gameControllerId = nullptr;
        Keyboard::_inputs = nullptr;
    }
    if(options.graphicsApi == GraphicsApi::OPENGL) {
        try {
            GLCorePatch::install(handle);
        } catch(const std::exception& ex) {
            Log::error("GLCOREPATCH", "Failed to apply glcorepatch: %s", ex.what());
            options.graphicsApi = GraphicsApi::OPENGL_ES2;
        }
    }

    Log::info("Launcher", "Initializing JNI");
    JniSupport support;
    FakeLooper::setJniSupport(&support);
    support.registerMinecraftNatives(+[](const char* sym) {
        return linker::dlsym(handle, sym);
    });
    std::thread startThread([&support]() {
        support.startGame((ANativeActivity_createFunc*)linker::dlsym(handle, "ANativeActivity_onCreate"),
                          linker::dlsym(handle, "stbi_load_from_memory"),
                          linker::dlsym(handle, "stbi_image_free"));
        linker::dlclose(handle);
    });
    startThread.detach();

    Log::info("Launcher", "Executing main thread");
    ThreadMover::executeMainThread();
    support.setLooperRunning(false);

    //    XboxLivePatches::workaroundShutdownFreeze(handle);
    XboxLiveHelper::getInstance().shutdown();
    // Workaround for XboxLive ShutdownFreeze
    _Exit(0);
    return 0;
}

void printVersionInfo() {
    printf("mcpelauncher-client %s / manifest %s\n", CLIENT_GIT_COMMIT_HASH, MANIFEST_GIT_COMMIT_HASH);
#if defined(__i386__) || defined(__x86_64__)
    CpuId cpuid;
    printf("CPU: %s %s\n", cpuid.getManufacturer(), cpuid.getBrandString());
    printf("SSSE3 support: %s\n", cpuid.queryFeatureFlag(CpuId::FeatureFlag::SSSE3) ? "YES" : "NO");
#endif
    auto windowManager = GameWindowManager::getManager();
    GraphicsApi graphicsApi = GLCorePatch::mustUseDesktopGL() ? GraphicsApi::OPENGL : GraphicsApi::OPENGL_ES2;
    auto window = windowManager->createWindow("mcpelauncher", 32, 32, graphicsApi);
    auto glGetString = (const char* (*)(int))windowManager->getProcAddrFunc()("glGetString");
    printf("GL Vendor: %s\n", glGetString(0x1F00 /* GL_VENDOR */));
    printf("GL Renderer: %s\n", glGetString(0x1F01 /* GL_RENDERER */));
    printf("GL Version: %s\n", glGetString(0x1F02 /* GL_VERSION */));
    printf("MSA daemon path: %s\n", XboxLiveHelper::findMsa().c_str());
}
