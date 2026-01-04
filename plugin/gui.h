#pragma once

#include <clap/clap.h>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

struct ImGuiContext;

namespace gui {

/// GUI state for a single plugin instance.
/// Manages native window, OpenGL context, and ImGui rendering.
struct PluginGui {
  // Platform-specific window/context
#ifdef _WIN32
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  HGLRC hglrc = nullptr;
#else
  Window window = 0;
  GLXContext glx_context = nullptr;
#endif
  ImGuiContext *imgui_ctx = nullptr;

  // Window state
  bool visible = false;
  uint32_t width = 400;
  uint32_t height = 300;

  // Callbacks - set by plugin
  std::function<void()> on_recompile;
  std::function<void()> on_open_folder;
  std::function<void(int, float)> on_param_changed;  // (param_id, value)

  // Status display
  std::string last_error;
  bool compile_success = true;

  // DSP file selection
  std::vector<std::string> dsp_files;  // Available .cc files
  int selected_file_index = 0;          // Currently selected index

  // Dynamic parameter callbacks
  std::function<int()> get_param_count;
  std::function<const char *(int)> get_param_name;
  std::function<float(int)> get_param_min;
  std::function<float(int)> get_param_max;
  std::function<float(int)> get_param_value;

  // Host references for timer support
  const clap_host_t *host = nullptr;
  const clap_plugin_t *plugin = nullptr;
  clap_id timer_id = CLAP_INVALID_ID;
};

/// Retrieves the GUI state from a plugin instance.
/// Defined in clap_plugin.cc.
PluginGui *get_gui(const clap_plugin_t *plugin);

// CLAP GUI extension callbacks
bool is_api_supported(const clap_plugin_t *plugin, const char *api, bool is_floating);
bool get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *is_floating);
bool create(const clap_plugin_t *plugin, const char *api, bool is_floating);
void destroy(const clap_plugin_t *plugin);
bool set_scale(const clap_plugin_t *plugin, double scale);
bool get_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height);
bool can_resize(const clap_plugin_t *plugin);
bool adjust_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height);
bool set_size(const clap_plugin_t *plugin, uint32_t width, uint32_t height);
bool set_parent(const clap_plugin_t *plugin, const clap_window_t *window);
bool set_transient(const clap_plugin_t *plugin, const clap_window_t *window);
void suggest_title(const clap_plugin_t *plugin, const char *title);
bool show(const clap_plugin_t *plugin);
bool hide(const clap_plugin_t *plugin);

/// Renders one frame of the ImGui interface.
/// Called from the host's timer callback at ~30fps.
void render(PluginGui *gui);

/// Scans the directory for .cc files and populates dsp_files list.
void scan_dsp_files(PluginGui *gui, const char *dir);

/// Returns the CLAP GUI extension struct.
const clap_plugin_gui_t *get_extension();

} // namespace gui
