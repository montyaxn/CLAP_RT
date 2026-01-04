#include "gui.h"

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
#endif

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace gui {

// ============================================================================
// Platform-specific globals and helpers
// ============================================================================

#ifdef _WIN32

static int g_instance_count = 0;
static const char *WINDOW_CLASS_NAME = "RT_CLAP_GUI";
static bool g_class_registered = false;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  PluginGui *gui = (PluginGui *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  if (gui && gui->imgui_ctx) {
    ImGui::SetCurrentContext(gui->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();

    switch (msg) {
      case WM_MOUSEMOVE:
        io.MousePos = ImVec2((float)LOWORD(lParam), (float)HIWORD(lParam));
        break;
      case WM_LBUTTONDOWN:
        io.MouseDown[0] = true;
        SetCapture(hwnd);
        break;
      case WM_LBUTTONUP:
        io.MouseDown[0] = false;
        ReleaseCapture();
        break;
      case WM_SIZE:
        gui->width = LOWORD(lParam);
        gui->height = HIWORD(lParam);
        io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);
        break;
    }
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}

static bool register_window_class() {
  if (g_class_registered)
    return true;

  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = WINDOW_CLASS_NAME;

  if (!RegisterClassEx(&wc))
    return false;

  g_class_registered = true;
  return true;
}

static bool create_win32_window(PluginGui *gui, HWND parent) {
  gui->hwnd = CreateWindowEx(
      0, WINDOW_CLASS_NAME, "RT_CLAP",
      WS_CHILD | WS_VISIBLE,
      0, 0, gui->width, gui->height,
      parent, nullptr, GetModuleHandle(nullptr), nullptr);

  if (!gui->hwnd)
    return false;

  SetWindowLongPtr(gui->hwnd, GWLP_USERDATA, (LONG_PTR)gui);

  // Create OpenGL context
  gui->hdc = GetDC(gui->hwnd);

  PIXELFORMATDESCRIPTOR pfd = {};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;

  int format = ChoosePixelFormat(gui->hdc, &pfd);
  if (!format) {
    ReleaseDC(gui->hwnd, gui->hdc);
    DestroyWindow(gui->hwnd);
    gui->hwnd = nullptr;
    return false;
  }

  SetPixelFormat(gui->hdc, format, &pfd);

  gui->hglrc = wglCreateContext(gui->hdc);
  if (!gui->hglrc) {
    ReleaseDC(gui->hwnd, gui->hdc);
    DestroyWindow(gui->hwnd);
    gui->hwnd = nullptr;
    return false;
  }

  return true;
}

static void init_imgui(PluginGui *gui) {
  wglMakeCurrent(gui->hdc, gui->hglrc);

  IMGUI_CHECKVERSION();
  gui->imgui_ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(gui->imgui_ctx);

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);

  ImGui::StyleColorsDark();
  ImGui_ImplOpenGL3_Init("#version 120");
}

#else // Linux/X11

static Display *g_display = nullptr;
static int g_instance_count = 0;

static bool init_x11() {
  if (g_display)
    return true;
  g_display = XOpenDisplay(nullptr);
  return g_display != nullptr;
}

static void shutdown_x11() {
  if (g_display && g_instance_count == 0) {
    XCloseDisplay(g_display);
    g_display = nullptr;
  }
}

static bool create_x11_window(PluginGui *gui, Window parent) {
  static int visual_attribs[] = {
    GLX_RGBA,
    GLX_DEPTH_SIZE, 24,
    GLX_DOUBLEBUFFER,
    None
  };

  int screen = DefaultScreen(g_display);
  XVisualInfo *vi = glXChooseVisual(g_display, screen, visual_attribs);
  if (!vi)
    return false;

  Colormap cmap = XCreateColormap(g_display, parent, vi->visual, AllocNone);

  XSetWindowAttributes swa;
  swa.colormap = cmap;
  swa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | StructureNotifyMask;

  gui->window = XCreateWindow(
    g_display, parent,
    0, 0, gui->width, gui->height,
    0, vi->depth, InputOutput, vi->visual,
    CWColormap | CWEventMask, &swa
  );

  if (!gui->window) {
    XFree(vi);
    return false;
  }

  gui->glx_context = glXCreateContext(g_display, vi, nullptr, GL_TRUE);
  XFree(vi);

  if (!gui->glx_context) {
    XDestroyWindow(g_display, gui->window);
    gui->window = 0;
    return false;
  }

  return true;
}

static void init_imgui(PluginGui *gui) {
  glXMakeCurrent(g_display, gui->window, gui->glx_context);

  IMGUI_CHECKVERSION();
  gui->imgui_ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(gui->imgui_ctx);

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);

  ImGui::StyleColorsDark();
  ImGui_ImplOpenGL3_Init("#version 120");
}

static void process_x11_events(PluginGui *gui) {
  while (XPending(g_display)) {
    XEvent event;
    XNextEvent(g_display, &event);

    ImGuiIO &io = ImGui::GetIO();

    switch (event.type) {
      case ButtonPress:
        if (event.xbutton.button == Button1)
          io.MouseDown[0] = true;
        break;
      case ButtonRelease:
        if (event.xbutton.button == Button1)
          io.MouseDown[0] = false;
        break;
      case MotionNotify:
        io.MousePos = ImVec2((float)event.xmotion.x, (float)event.xmotion.y);
        break;
      case ConfigureNotify:
        gui->width = event.xconfigure.width;
        gui->height = event.xconfigure.height;
        io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);
        break;
    }
  }
}

#endif // _WIN32

// ============================================================================
// Platform-independent code
// ============================================================================

void scan_dsp_files(PluginGui *gui, const char *dir) {
  gui->dsp_files.clear();
  gui->selected_file_index = 0;

  std::error_code ec;
  std::filesystem::path base_dir(dir);

  for (const auto &subdir : std::filesystem::directory_iterator(base_dir, ec)) {
    if (!subdir.is_directory()) continue;

    std::string folder_name = subdir.path().filename().string();
    if (folder_name == "lib") continue;

    for (const auto &file : std::filesystem::directory_iterator(subdir.path(), ec)) {
      if (file.path().extension() == ".cc") {
        std::string relative_path = folder_name + "/" + file.path().filename().string();
        gui->dsp_files.push_back(relative_path);
      }
    }
  }

  std::sort(gui->dsp_files.begin(), gui->dsp_files.end());

  for (size_t i = 0; i < gui->dsp_files.size(); i++) {
    if (gui->dsp_files[i] == "local/gain.cc") {
      gui->selected_file_index = static_cast<int>(i);
      break;
    }
  }
}

// ============================================================================
// CLAP GUI Extension Callbacks
// ============================================================================

bool is_api_supported(const clap_plugin_t *plugin, const char *api, bool is_floating) {
  (void)plugin;
#ifdef _WIN32
  return strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !is_floating;
#else
  return strcmp(api, CLAP_WINDOW_API_X11) == 0 && !is_floating;
#endif
}

bool get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *is_floating) {
  (void)plugin;
#ifdef _WIN32
  *api = CLAP_WINDOW_API_WIN32;
#else
  *api = CLAP_WINDOW_API_X11;
#endif
  *is_floating = false;
  return true;
}

bool create(const clap_plugin_t *plugin, const char *api, bool is_floating) {
  (void)is_floating;

#ifdef _WIN32
  if (strcmp(api, CLAP_WINDOW_API_WIN32) != 0)
    return false;

  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

  if (!register_window_class())
    return false;

  g_instance_count++;
  return true;
#else
  if (strcmp(api, CLAP_WINDOW_API_X11) != 0)
    return false;

  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

  if (!init_x11())
    return false;

  g_instance_count++;
  return true;
#endif
}

void destroy(const clap_plugin_t *plugin) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return;

#ifdef _WIN32
  if (gui->hglrc) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(gui->hglrc);
    gui->hglrc = nullptr;
  }

  if (gui->hdc && gui->hwnd) {
    ReleaseDC(gui->hwnd, gui->hdc);
    gui->hdc = nullptr;
  }

  if (gui->hwnd) {
    DestroyWindow(gui->hwnd);
    gui->hwnd = nullptr;
  }
#else
  if (gui->glx_context) {
    glXMakeCurrent(g_display, None, nullptr);
    glXDestroyContext(g_display, gui->glx_context);
    gui->glx_context = nullptr;
  }

  if (gui->window) {
    XDestroyWindow(g_display, gui->window);
    gui->window = 0;
  }
#endif

  if (gui->imgui_ctx) {
    ImGui::SetCurrentContext(gui->imgui_ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(gui->imgui_ctx);
    gui->imgui_ctx = nullptr;
  }

  g_instance_count--;
#ifndef _WIN32
  if (g_instance_count == 0) {
    shutdown_x11();
  }
#endif
}

bool set_scale(const clap_plugin_t *plugin, double scale) {
  (void)plugin;
  (void)scale;
  return false;
}

bool get_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return false;
  *width = gui->width;
  *height = gui->height;
  return true;
}

bool can_resize(const clap_plugin_t *plugin) {
  (void)plugin;
  return true;
}

bool adjust_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
  (void)plugin;
  if (*width < 200)
    *width = 200;
  if (*height < 100)
    *height = 100;
  return true;
}

bool set_size(const clap_plugin_t *plugin, uint32_t width, uint32_t height) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

  gui->width = width;
  gui->height = height;

#ifdef _WIN32
  if (gui->hwnd) {
    SetWindowPos(gui->hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
  }
#else
  if (gui->window && g_display) {
    XResizeWindow(g_display, gui->window, width, height);
  }
#endif
  return true;
}

bool set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

#ifdef _WIN32
  if (!window || strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0)
    return false;

  HWND parent = (HWND)window->win32;
  if (!create_win32_window(gui, parent))
    return false;
#else
  if (!window || strcmp(window->api, CLAP_WINDOW_API_X11) != 0)
    return false;

  if (!g_display)
    return false;

  Window parent = (Window)window->x11;
  if (!create_x11_window(gui, parent))
    return false;
#endif

  init_imgui(gui);
  return true;
}

bool set_transient(const clap_plugin_t *plugin, const clap_window_t *window) {
  (void)plugin;
  (void)window;
  return false;
}

void suggest_title(const clap_plugin_t *plugin, const char *title) {
  (void)plugin;
  (void)title;
}

bool show(const clap_plugin_t *plugin) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

#ifdef _WIN32
  if (!gui->hwnd)
    return false;
  ShowWindow(gui->hwnd, SW_SHOW);
#else
  if (!gui->window || !g_display)
    return false;
  XMapWindow(g_display, gui->window);
  XFlush(g_display);
#endif

  gui->visible = true;

  // Register timer for rendering (~30fps)
  if (gui->host && gui->timer_id == CLAP_INVALID_ID) {
    auto *timer_ext = static_cast<const clap_host_timer_support_t *>(
        gui->host->get_extension(gui->host, CLAP_EXT_TIMER_SUPPORT));
    if (timer_ext && timer_ext->register_timer) {
      timer_ext->register_timer(gui->host, 30, &gui->timer_id);
    }
  }

  return true;
}

bool hide(const clap_plugin_t *plugin) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

  // Unregister timer
  if (gui->host && gui->timer_id != CLAP_INVALID_ID) {
    auto *timer_ext = static_cast<const clap_host_timer_support_t *>(
        gui->host->get_extension(gui->host, CLAP_EXT_TIMER_SUPPORT));
    if (timer_ext && timer_ext->unregister_timer) {
      timer_ext->unregister_timer(gui->host, gui->timer_id);
      gui->timer_id = CLAP_INVALID_ID;
    }
  }

#ifdef _WIN32
  if (!gui->hwnd)
    return false;
  ShowWindow(gui->hwnd, SW_HIDE);
#else
  if (!gui->window || !g_display)
    return false;
  XUnmapWindow(g_display, gui->window);
  XFlush(g_display);
#endif

  gui->visible = false;
  return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void draw_gui_content(PluginGui *gui) {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2((float)gui->width, (float)gui->height));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoScrollbar;

  ImGui::Begin("JIT DSP", nullptr, flags);

  ImGui::Text("=== BUILD %s %s ===", __DATE__, __TIME__);
  ImGui::Spacing();

  if (ImGui::Button("Recompile", ImVec2(120, 40))) {
    if (gui->on_recompile) {
      gui->on_recompile();
    }
  }

  ImGui::SameLine();

  if (ImGui::Button("Open Folder", ImVec2(120, 40))) {
    if (gui->on_open_folder) {
      gui->on_open_folder();
    }
  }

  ImGui::Spacing();

  if (!gui->last_error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::TextWrapped("Error: %s", gui->last_error.c_str());
    ImGui::PopStyleColor();
  } else if (gui->compile_success) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    ImGui::Text("Compiled successfully");
    ImGui::PopStyleColor();
  }

  ImGui::Separator();
  ImGui::Text("JIT DSP - Hot Reload");

  // DSP file selector
  ImGui::Text("Files found: %zu", gui->dsp_files.size());
  if (!gui->dsp_files.empty()) {
    const char *current = gui->dsp_files[gui->selected_file_index].c_str();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("DSP File", current)) {
      for (int i = 0; i < static_cast<int>(gui->dsp_files.size()); i++) {
        bool is_selected = (gui->selected_file_index == i);
        if (ImGui::Selectable(gui->dsp_files[i].c_str(), is_selected)) {
          gui->selected_file_index = i;
          if (gui->on_recompile) {
            gui->on_recompile();
          }
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  } else {
    ImGui::Text("No .cc files found");
  }

  // Parameters section
  ImGui::Separator();
  ImGui::Text("Parameters");
  ImGui::Spacing();

  int param_count = gui->get_param_count ? gui->get_param_count() : 0;
  if (param_count == 0) {
    ImGui::TextDisabled("No parameters defined");
  }

  for (int i = 0; i < param_count; ++i) {
    const char *name = gui->get_param_name ? gui->get_param_name(i) : "?";
    float min_val = gui->get_param_min ? gui->get_param_min(i) : 0.0f;
    float max_val = gui->get_param_max ? gui->get_param_max(i) : 1.0f;
    float value = gui->get_param_value ? gui->get_param_value(i) : 0.0f;

    ImGui::PushID(i);
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat(name, &value, min_val, max_val, "%.2f")) {
      if (gui->on_param_changed) {
        gui->on_param_changed(i, value);
      }
    }
    ImGui::PopID();
  }

  ImGui::End();
}

void render(PluginGui *gui) {
  if (!gui || !gui->visible || !gui->imgui_ctx)
    return;

#ifdef _WIN32
  if (!gui->hwnd || !gui->hglrc)
    return;

  // Process Windows messages
  MSG msg;
  while (PeekMessage(&msg, gui->hwnd, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  wglMakeCurrent(gui->hdc, gui->hglrc);
#else
  if (!gui->window || !gui->glx_context || !g_display)
    return;

  glXMakeCurrent(g_display, gui->window, gui->glx_context);
  process_x11_events(gui);
#endif

  ImGui::SetCurrentContext(gui->imgui_ctx);

  // Start ImGui frame
  ImGuiIO &io = ImGui::GetIO();
  io.DeltaTime = 1.0f / 30.0f;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();

  draw_gui_content(gui);

  // Render
  ImGui::Render();

  glViewport(0, 0, gui->width, gui->height);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#ifdef _WIN32
  SwapBuffers(gui->hdc);
#else
  glXSwapBuffers(g_display, gui->window);
#endif
}

// ============================================================================
// Extension Struct
// ============================================================================

static const clap_plugin_gui_t s_gui_extension = {
    .is_api_supported = is_api_supported,
    .get_preferred_api = get_preferred_api,
    .create = create,
    .destroy = destroy,
    .set_scale = set_scale,
    .get_size = get_size,
    .can_resize = can_resize,
    .get_resize_hints = nullptr,
    .adjust_size = adjust_size,
    .set_size = set_size,
    .set_parent = set_parent,
    .set_transient = set_transient,
    .suggest_title = suggest_title,
    .show = show,
    .hide = hide,
};

const clap_plugin_gui_t *get_extension() { return &s_gui_extension; }

} // namespace gui
