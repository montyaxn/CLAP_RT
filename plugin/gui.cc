#include "gui.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace gui {

// ============================================================================
// X11/Display Management
// ============================================================================

static Display *g_display = nullptr;
static int g_instance_count = 0;

/// Opens the X11 display connection if not already open.
static bool init_x11() {
  if (g_display)
    return true;
  g_display = XOpenDisplay(nullptr);
  return g_display != nullptr;
}

/// Closes the X11 display connection when no more instances exist.
static void shutdown_x11() {
  if (g_display && g_instance_count == 0) {
    XCloseDisplay(g_display);
    g_display = nullptr;
  }
}

/// Scans the directory for .cc files and populates dsp_files list.
void scan_dsp_files(PluginGui *gui, const char *dir) {
  gui->dsp_files.clear();
  gui->selected_file_index = 0;

  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".cc") {
      gui->dsp_files.push_back(entry.path().filename().string());
    }
  }

  std::sort(gui->dsp_files.begin(), gui->dsp_files.end());

  // Default to "dsp.cc" if it exists
  for (size_t i = 0; i < gui->dsp_files.size(); i++) {
    if (gui->dsp_files[i] == "dsp.cc") {
      gui->selected_file_index = static_cast<int>(i);
      break;
    }
  }
}

/// Creates an X11 child window with GLX context.
/// Returns true on success, false on failure.
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

/// Initializes ImGui with OpenGL backend.
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

// ============================================================================
// CLAP GUI Extension Callbacks
// ============================================================================

bool is_api_supported(const clap_plugin_t *plugin, const char *api,
                      bool is_floating) {
  (void)plugin;
  // Only support embedded X11 windows
  return strcmp(api, CLAP_WINDOW_API_X11) == 0 && !is_floating;
}

bool get_preferred_api(const clap_plugin_t *plugin, const char **api,
                       bool *is_floating) {
  (void)plugin;
  *api = CLAP_WINDOW_API_X11;
  *is_floating = false;
  return true;
}

bool create(const clap_plugin_t *plugin, const char *api, bool is_floating) {
  (void)is_floating;

  if (strcmp(api, CLAP_WINDOW_API_X11) != 0)
    return false;

  auto *gui = get_gui(plugin);
  if (!gui)
    return false;

  if (!init_x11())
    return false;

  g_instance_count++;
  return true;
}

void destroy(const clap_plugin_t *plugin) {
  auto *gui = get_gui(plugin);
  if (!gui)
    return;

  if (gui->glx_context) {
    glXMakeCurrent(g_display, None, nullptr);
    glXDestroyContext(g_display, gui->glx_context);
    gui->glx_context = nullptr;
  }

  if (gui->window) {
    XDestroyWindow(g_display, gui->window);
    gui->window = 0;
  }

  if (gui->imgui_ctx) {
    ImGui::SetCurrentContext(gui->imgui_ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(gui->imgui_ctx);
    gui->imgui_ctx = nullptr;
  }

  g_instance_count--;
  if (g_instance_count == 0) {
    shutdown_x11();
  }
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

  if (gui->window && g_display) {
    XResizeWindow(g_display, gui->window, width, height);
  }
  return true;
}

/// Creates an X11 child window inside the host's parent window.
/// Called by the host after create() to provide the parent window handle.
bool set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
  if (!window || strcmp(window->api, CLAP_WINDOW_API_X11) != 0)
    return false;

  auto *gui = get_gui(plugin);
  if (!gui || !g_display)
    return false;

  Window parent = (Window)window->x11;

  if (!create_x11_window(gui, parent))
    return false;

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
  if (!gui || !gui->window || !g_display)
    return false;

  XMapWindow(g_display, gui->window);
  XFlush(g_display);
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
  if (!gui || !gui->window || !g_display)
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

  XUnmapWindow(g_display, gui->window);
  XFlush(g_display);
  gui->visible = false;
  return true;
}

// ============================================================================
// Rendering
// ============================================================================

/// Processes pending X11 events and updates ImGui input state.
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

/// Draws the ImGui interface content.
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

  ImGui::End();
}

/// Renders one frame of the ImGui interface.
/// Called from the host's timer callback at ~30fps.
void render(PluginGui *gui) {
  if (!gui || !gui->window || !gui->visible || !gui->glx_context || !g_display)
    return;

  glXMakeCurrent(g_display, gui->window, gui->glx_context);
  ImGui::SetCurrentContext(gui->imgui_ctx);

  process_x11_events(gui);

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

  glXSwapBuffers(g_display, gui->window);
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
