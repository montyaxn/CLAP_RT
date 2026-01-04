#include <clap/clap.h>

#include "../jit/JIT.h"
#include "gui.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>

// ============================================================================
// Types and Globals
// ============================================================================

/// Directory containing DSP source files (~/.local/share/rt-clap/)
static std::filesystem::path g_dsp_dir;

/// Global parameter array - DSP reads directly for performance
/// Exported so JIT-compiled DSP code can access via extern
float g_params[16] = {1.0f};  // [0] = gain, default 1.0

/// DSP function signatures
using ProcessFn = void (*)(const float *const *, float *const *, uint32_t,
                           uint32_t);
using InitFn = bool (*)(double, uint32_t, uint32_t);
using DestroyFn = void (*)();

/// DSP parameter query functions
using ParamCountFn = int (*)();
using ParamNameFn = const char *(*)(int);
using ParamFloatFn = float (*)(int);

/// Parameter info from DSP
struct ParamInfo {
  std::string name;
  float min_value = 0.0f;
  float max_value = 1.0f;
  float default_value = 0.5f;
};

/// Per-instance plugin state
struct PluginState {
  std::unique_ptr<clap_rt::ClapJIT> jit;
  std::atomic<ProcessFn> process_fn{nullptr};
  const clap_host_t *host = nullptr;
  const clap_plugin_t *plugin = nullptr;

  // Hot-reload support (atomic swap at frame boundary)
  std::atomic<bool> reload_pending{false};
  ProcessFn pending_fn = nullptr;
  std::unique_ptr<clap_rt::ClapJIT> pending_jit;  // New JIT, swapped at frame boundary

  // DSP lifecycle functions (optional)
  InitFn dsp_init = nullptr;
  DestroyFn dsp_destroy = nullptr;

  // Pending lifecycle functions for hot-reload
  InitFn pending_init = nullptr;
  DestroyFn pending_destroy = nullptr;

  // Audio parameters (stored for hot-reload init calls)
  double sample_rate = 0;
  uint32_t min_frames = 0;
  uint32_t max_frames = 0;
  bool dsp_activated = false;

  // Dynamic parameters from DSP
  std::vector<ParamInfo> param_info;
  std::vector<float> gui_params;  // GUI writes, process reads (synced each frame)

  // File watching for auto-reload
  std::filesystem::file_time_type last_modified{};
  std::filesystem::file_time_type folder_modified{};
  clap_id timer_id = CLAP_INVALID_ID;

  // GUI state
  gui::PluginGui gui_state;
};

/// Plugin descriptor
static const clap_plugin_descriptor_t plugin_descriptor = {
    .clap_version = CLAP_VERSION,
    .id = "com.rt-clap.jit-dsp",
    .name = "JIT DSP",
    .vendor = "RT_CLAP",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "JIT-compiled DSP plugin",
    .features = (const char *[]){CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                 CLAP_PLUGIN_FEATURE_UTILITY, nullptr},
};

// ============================================================================
// Helper Functions
// ============================================================================

/// Retrieves PluginState from a clap_plugin pointer.
static PluginState *get_state(const clap_plugin_t *plugin) {
  return static_cast<PluginState *>(plugin->plugin_data);
}

/// Retrieves GUI state from a plugin (required by gui.cc).
namespace gui {
PluginGui *get_gui(const clap_plugin_t *plugin) {
  auto *state = get_state(plugin);
  return state ? &state->gui_state : nullptr;
}
} // namespace gui

/// Returns the currently selected DSP filename.
static std::string get_selected_dsp_file(PluginState *state) {
  if (state->gui_state.dsp_files.empty()) {
    return "dsp.cc";
  }
  return state->gui_state.dsp_files[state->gui_state.selected_file_index];
}

/// Logs compilation errors to ~/.local/share/rt-clap/compile.log
static void log_compile(const std::string &msg) {
  auto log_path = g_dsp_dir / "compile.log";
  if (auto f = fopen(log_path.string().c_str(), "a")) {
    fprintf(f, "%s\n", msg.c_str());
    fclose(f);
  }
}

/// Scans lib/ folder and returns list of .cc files
static std::vector<std::string> get_lib_sources() {
  std::vector<std::string> sources;
  auto lib_dir = g_dsp_dir / "lib";
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(lib_dir, ec)) {
    if (entry.path().extension() == ".cc") {
      sources.push_back(entry.path().string());
    }
  }
  return sources;
}

/// Result of a compilation attempt
struct CompileResult {
  std::unique_ptr<clap_rt::ClapJIT> jit;
  ProcessFn process_fn = nullptr;
  InitFn init_fn = nullptr;
  DestroyFn destroy_fn = nullptr;

  // Parameter query functions (optional)
  ParamCountFn param_count = nullptr;
  ParamNameFn param_name = nullptr;
  ParamFloatFn param_min = nullptr;
  ParamFloatFn param_max = nullptr;
  ParamFloatFn param_default = nullptr;

  std::string error;

  bool success() const { return process_fn != nullptr; }
};

/// Compiles DSP code and returns the result.
/// Handles lib/ sources and the main DSP file.
static CompileResult compile_dsp(const std::filesystem::path &dsp_path) {
  CompileResult result;
  auto lib_dir = g_dsp_dir / "lib";

  log_compile("Compiling: " + dsp_path.string());

  // Set up JIT options with lib/ as include path
  clap_rt::JITOptions opts;
  if (std::filesystem::exists(lib_dir)) {
    opts.includePaths.push_back(lib_dir.string());
  }

  // Set up cache directory
  if (const char *home = std::getenv("HOME")) {
    opts.cacheDir = (std::filesystem::path(home) / ".cache" / "rt-clap").string();
  }

  // Create JIT instance
  auto jit_or_err = clap_rt::ClapJIT::create(opts);
  if (!jit_or_err) {
    result.error = llvm::toString(jit_or_err.takeError());
    log_compile("JIT create error: " + result.error);
    return result;
  }

  result.jit = std::make_unique<clap_rt::ClapJIT>(std::move(*jit_or_err));

  // Define g_params symbol so DSP code can access it
  if (auto err = result.jit->defineSymbol("g_params", g_params)) {
    result.error = llvm::toString(std::move(err));
    log_compile("Symbol define error: " + result.error);
    result.jit.reset();
    return result;
  }

  // Compile lib/ sources first
  for (const auto &lib_src : get_lib_sources()) {
    log_compile("Compiling lib: " + lib_src);
    auto err = result.jit->addModule(lib_src);
    if (err) {
      result.error = llvm::toString(std::move(err));
      log_compile("Lib compile error: " + result.error);
      result.jit.reset();
      return result;
    }
  }

  // Compile DSP code
  auto err = result.jit->addModule(dsp_path.string());
  if (err) {
    result.error = llvm::toString(std::move(err));
    log_compile("Compile error: " + result.error);
    result.jit.reset();
    return result;
  }

  // Lookup process function (required)
  auto fn_or_err = result.jit->lookupAs<void(const float *const *, float *const *,
                                             uint32_t, uint32_t)>("process");
  if (!fn_or_err) {
    result.error = llvm::toString(fn_or_err.takeError());
    log_compile("Lookup error: " + result.error);
    result.jit.reset();
    return result;
  }
  result.process_fn = *fn_or_err;

  // Lookup optional init function
  auto init_or_err = result.jit->lookupAs<bool(double, uint32_t, uint32_t)>("init");
  if (init_or_err) {
    result.init_fn = *init_or_err;
    log_compile("Found init()");
  } else {
    llvm::consumeError(init_or_err.takeError());
  }

  // Lookup optional destroy function
  auto destroy_or_err = result.jit->lookupAs<void()>("destroy");
  if (destroy_or_err) {
    result.destroy_fn = *destroy_or_err;
    log_compile("Found destroy()");
  } else {
    llvm::consumeError(destroy_or_err.takeError());
  }

  // Lookup optional parameter query functions
  if (auto fn = result.jit->lookupAs<int()>("param_count")) {
    result.param_count = *fn;
    log_compile("Found param_count()");
  } else {
    llvm::consumeError(fn.takeError());
  }

  if (auto fn = result.jit->lookupAs<const char *(int)>("param_name")) {
    result.param_name = *fn;
  } else {
    llvm::consumeError(fn.takeError());
  }

  if (auto fn = result.jit->lookupAs<float(int)>("param_min")) {
    result.param_min = *fn;
  } else {
    llvm::consumeError(fn.takeError());
  }

  if (auto fn = result.jit->lookupAs<float(int)>("param_max")) {
    result.param_max = *fn;
  } else {
    llvm::consumeError(fn.takeError());
  }

  if (auto fn = result.jit->lookupAs<float(int)>("param_default")) {
    result.param_default = *fn;
  } else {
    llvm::consumeError(fn.takeError());
  }

  log_compile("Compile success!");
  return result;
}

/// Queries DSP for parameter definitions and updates plugin state.
static void query_dsp_params(PluginState *state, const CompileResult &result) {
  state->param_info.clear();
  state->gui_params.clear();

  if (!result.param_count) {
    log_compile("No param_count() - using 0 parameters");
    return;
  }

  int count = result.param_count();
  log_compile("DSP defines " + std::to_string(count) + " parameters");

  for (int i = 0; i < count && i < 16; ++i) {
    ParamInfo info;
    info.name = result.param_name ? result.param_name(i) : "Param";
    info.min_value = result.param_min ? result.param_min(i) : 0.0f;
    info.max_value = result.param_max ? result.param_max(i) : 1.0f;
    info.default_value = result.param_default ? result.param_default(i) : 0.5f;

    state->param_info.push_back(info);
    state->gui_params.push_back(info.default_value);
    g_params[i] = info.default_value;

    log_compile("  [" + std::to_string(i) + "] " + info.name +
                " (" + std::to_string(info.min_value) + " - " +
                std::to_string(info.max_value) + ", default " +
                std::to_string(info.default_value) + ")");
  }
}

/// Recompiles the DSP code from the selected file.
/// Updates GUI state with success/error status.
/// Uses atomic swap to safely update the process function pointer.
static void do_recompile(PluginState *state) {
  state->gui_state.last_error.clear();
  state->gui_state.compile_success = false;

  auto dsp_path = g_dsp_dir / get_selected_dsp_file(state);
  auto result = compile_dsp(dsp_path);

  if (!result.success()) {
    state->gui_state.last_error = result.error;
    return;
  }

  // Set pending functions and JIT for atomic swap at frame boundary
  // IMPORTANT: Don't replace state->jit yet - old JIT must stay alive
  // until plugin_process() calls the old destroy() function
  state->pending_fn = result.process_fn;
  state->pending_init = result.init_fn;
  state->pending_destroy = result.destroy_fn;
  state->pending_jit = std::move(result.jit);

  // Query params from new DSP (before swap, but params are just metadata)
  size_t old_count = state->param_info.size();
  query_dsp_params(state, result);

  // Notify host if param structure changed
  if (state->param_info.size() != old_count) {
    auto *params_host = static_cast<const clap_host_params_t *>(
        state->host->get_extension(state->host, CLAP_EXT_PARAMS));
    if (params_host && params_host->rescan) {
      params_host->rescan(state->host, CLAP_PARAM_RESCAN_ALL);
    }
  }

  state->reload_pending.store(true, std::memory_order_release);
  state->gui_state.compile_success = true;

  // Reset file watcher timestamp to avoid double-compile on file switch
  state->last_modified = std::filesystem::file_time_type{};
}

// ============================================================================
// Plugin Lifecycle
// ============================================================================

static bool plugin_init(const clap_plugin_t *plugin) {
  auto *state = get_state(plugin);
  state->plugin = plugin;

  log_compile("=== plugin_init ===");

  // Set up GUI state
  state->gui_state.host = state->host;
  state->gui_state.plugin = plugin;
  state->gui_state.on_recompile = [state]() {
    do_recompile(state);
  };
  state->gui_state.on_open_folder = []() {
    std::string cmd = "xdg-open \"" + g_dsp_dir.string() + "\" &";
    std::system(cmd.c_str());
  };
  state->gui_state.on_param_changed = [state](int id, float value) {
    if (id >= 0 && id < static_cast<int>(state->gui_params.size())) {
      state->gui_params[id] = value;
    }
  };
  state->gui_state.get_param_value = [state](int id) -> float {
    if (id >= 0 && id < static_cast<int>(state->gui_params.size())) {
      return state->gui_params[id];
    }
    return 0.0f;
  };
  state->gui_state.get_param_count = [state]() -> int {
    return static_cast<int>(state->param_info.size());
  };
  state->gui_state.get_param_name = [state](int id) -> const char * {
    if (id >= 0 && id < static_cast<int>(state->param_info.size())) {
      return state->param_info[id].name.c_str();
    }
    return "?";
  };
  state->gui_state.get_param_min = [state](int id) -> float {
    if (id >= 0 && id < static_cast<int>(state->param_info.size())) {
      return state->param_info[id].min_value;
    }
    return 0.0f;
  };
  state->gui_state.get_param_max = [state](int id) -> float {
    if (id >= 0 && id < static_cast<int>(state->param_info.size())) {
      return state->param_info[id].max_value;
    }
    return 1.0f;
  };

  // Scan for available DSP files
  gui::scan_dsp_files(&state->gui_state, g_dsp_dir.string().c_str());
  log_compile("Found " + std::to_string(state->gui_state.dsp_files.size()) + " DSP files");

  // Initialize LLVM (safe to call multiple times)
  clap_rt::ClapJIT::initializeLLVM();

  // Compile DSP code
  auto dsp_path = g_dsp_dir / get_selected_dsp_file(state);
  auto result = compile_dsp(dsp_path);

  if (!result.success()) {
    log_compile("Init failed: " + result.error);
    return false;
  }

  state->jit = std::move(result.jit);
  state->process_fn.store(result.process_fn, std::memory_order_release);
  state->dsp_init = result.init_fn;
  state->dsp_destroy = result.destroy_fn;

  // Query DSP for parameter definitions
  query_dsp_params(state, result);

  log_compile("Init success!");

  // Register timer for file watching (500ms interval)
  auto *timer_ext = static_cast<const clap_host_timer_support_t *>(
      state->host->get_extension(state->host, CLAP_EXT_TIMER_SUPPORT));
  if (timer_ext && timer_ext->register_timer) {
    timer_ext->register_timer(state->host, 500, &state->timer_id);
  }

  return true;
}

static void plugin_destroy(const clap_plugin_t *plugin) {
  auto *state = get_state(plugin);

  // Call DSP destroy if still activated (shouldn't happen, but be safe)
  if (state->dsp_activated && state->dsp_destroy) {
    state->dsp_destroy();
    state->dsp_activated = false;
  }

  // Destroy GUI
  gui::destroy(plugin);

  // Unregister file-watching timer
  if (state->timer_id != CLAP_INVALID_ID && state->host) {
    auto *timer_ext = static_cast<const clap_host_timer_support_t *>(
        state->host->get_extension(state->host, CLAP_EXT_TIMER_SUPPORT));
    if (timer_ext && timer_ext->unregister_timer) {
      timer_ext->unregister_timer(state->host, state->timer_id);
    }
  }

  delete state;
}

static bool plugin_activate(const clap_plugin_t *plugin, double sample_rate,
                            uint32_t min_frames, uint32_t max_frames) {
  auto *state = get_state(plugin);

  // Store audio parameters for hot-reload
  state->sample_rate = sample_rate;
  state->min_frames = min_frames;
  state->max_frames = max_frames;

  // Call DSP init if present
  if (state->dsp_init) {
    if (!state->dsp_init(sample_rate, min_frames, max_frames)) {
      log_compile("DSP init() returned false");
      return false;
    }
    log_compile("DSP init() called");
  }

  state->dsp_activated = true;
  return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) {
  auto *state = get_state(plugin);

  // Call DSP destroy if present
  if (state->dsp_activated && state->dsp_destroy) {
    state->dsp_destroy();
    log_compile("DSP destroy() called");
  }

  state->dsp_activated = false;
}

static bool plugin_start_processing(const clap_plugin_t *plugin) {
  (void)plugin;
  return true;
}

static void plugin_stop_processing(const clap_plugin_t *plugin) {
  (void)plugin;
}

static void plugin_reset(const clap_plugin_t *plugin) {
  (void)plugin;
}

// ============================================================================
// Audio Processing
// ============================================================================

static clap_process_status plugin_process(const clap_plugin_t *plugin,
                                          const clap_process_t *process) {
  auto *state = get_state(plugin);

  // Check for hot-reload at frame boundary
  if (state->reload_pending.load(std::memory_order_acquire)) {
    // Call old destroy before swapping (old JIT still alive here)
    if (state->dsp_activated && state->dsp_destroy) {
      state->dsp_destroy();
    }

    // Swap JIT instance (destroys old JIT - safe because we already called destroy)
    state->jit = std::move(state->pending_jit);

    // Swap function pointers
    state->process_fn.store(state->pending_fn, std::memory_order_release);
    state->dsp_init = state->pending_init;
    state->dsp_destroy = state->pending_destroy;
    state->reload_pending.store(false, std::memory_order_release);

    // Call new init after swap (new JIT now active)
    if (state->dsp_activated && state->dsp_init) {
      state->dsp_init(state->sample_rate, state->min_frames, state->max_frames);
    }
  }

  // Sync GUI parameters to global array
  for (size_t i = 0; i < state->gui_params.size(); ++i) {
    g_params[i] = state->gui_params[i];
  }

  // Process incoming CLAP parameter events from host
  if (process->in_events) {
    for (uint32_t i = 0; i < process->in_events->size(process->in_events); ++i) {
      auto *event = process->in_events->get(process->in_events, i);
      if (event->space_id != CLAP_CORE_EVENT_SPACE_ID)
        continue;
      if (event->type == CLAP_EVENT_PARAM_VALUE) {
        auto *pv = reinterpret_cast<const clap_event_param_value_t *>(event);
        if (pv->param_id < state->gui_params.size()) {
          g_params[pv->param_id] = static_cast<float>(pv->value);
          state->gui_params[pv->param_id] = g_params[pv->param_id];
        }
      }
    }
  }

  ProcessFn fn = state->process_fn.load(std::memory_order_acquire);
  if (!fn)
    return CLAP_PROCESS_ERROR;

  // Safety checks
  if (!process->audio_inputs || !process->audio_outputs)
    return CLAP_PROCESS_ERROR;
  if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
    return CLAP_PROCESS_ERROR;

  const uint32_t num_frames = process->frames_count;
  const uint32_t in_channels = process->audio_inputs[0].channel_count;
  const uint32_t out_channels = process->audio_outputs[0].channel_count;
  const uint32_t num_channels = in_channels < out_channels ? in_channels : out_channels;

  if (num_channels == 0 || num_frames == 0)
    return CLAP_PROCESS_CONTINUE;

  // Call JIT'd process function
  fn(process->audio_inputs[0].data32, process->audio_outputs[0].data32,
     num_channels, num_frames);

  return CLAP_PROCESS_CONTINUE;
}

// ============================================================================
// Extensions
// ============================================================================

// --- Audio Ports ---

static uint32_t audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
  (void)plugin;
  (void)is_input;
  return 1; // One stereo port for both input and output
}

static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index,
                            bool is_input, clap_audio_port_info_t *info) {
  (void)plugin;
  if (index != 0)
    return false;

  info->id = is_input ? 0 : 1;
  snprintf(info->name, sizeof(info->name), "%s", is_input ? "Input" : "Output");
  info->channel_count = 2;
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = is_input ? 1 : 0;
  return true;
}

static const clap_plugin_audio_ports_t audio_ports_extension = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

// --- Parameters ---

static uint32_t params_count(const clap_plugin_t *plugin) {
  auto *state = get_state(plugin);
  return static_cast<uint32_t>(state->param_info.size());
}

static bool params_get_info(const clap_plugin_t *plugin, uint32_t index,
                            clap_param_info_t *info) {
  auto *state = get_state(plugin);
  if (index >= state->param_info.size())
    return false;

  const auto &p = state->param_info[index];
  info->id = index;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  strncpy(info->name, p.name.c_str(), CLAP_NAME_SIZE);
  strncpy(info->module, "", CLAP_PATH_SIZE);
  info->min_value = p.min_value;
  info->max_value = p.max_value;
  info->default_value = p.default_value;
  return true;
}

static bool params_get_value(const clap_plugin_t *plugin, clap_id param_id,
                             double *value) {
  auto *state = get_state(plugin);
  if (param_id >= state->param_info.size())
    return false;
  *value = g_params[param_id];
  return true;
}

static bool params_value_to_text(const clap_plugin_t *plugin, clap_id param_id,
                                 double value, char *buf, uint32_t size) {
  auto *state = get_state(plugin);
  if (param_id >= state->param_info.size())
    return false;
  snprintf(buf, size, "%.2f", value);
  return true;
}

static bool params_text_to_value(const clap_plugin_t *plugin, clap_id param_id,
                                 const char *text, double *value) {
  (void)plugin;
  (void)param_id;
  (void)text;
  (void)value;
  return false;  // Not implemented
}

static void params_flush(const clap_plugin_t *plugin,
                         const clap_input_events_t *in,
                         const clap_output_events_t *out) {
  (void)out;
  auto *state = get_state(plugin);

  for (uint32_t i = 0; i < in->size(in); ++i) {
    auto *event = in->get(in, i);
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID)
      continue;
    if (event->type == CLAP_EVENT_PARAM_VALUE) {
      auto *pv = reinterpret_cast<const clap_event_param_value_t *>(event);
      if (pv->param_id < state->param_info.size()) {
        g_params[pv->param_id] = static_cast<float>(pv->value);
        state->gui_params[pv->param_id] = g_params[pv->param_id];
      }
    }
  }
}

static const clap_plugin_params_t params_extension = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

// --- Timer Support ---

/// Timer callback handling both file watching and GUI rendering.
static void timer_on_timer(const clap_plugin_t *plugin, clap_id timer_id) {
  auto *state = get_state(plugin);

  // Handle GUI timer
  if (timer_id == state->gui_state.timer_id) {
    gui::render(&state->gui_state);
    return;
  }

  // Handle file watching timer
  if (timer_id == state->timer_id) {
    std::error_code ec;

    // Watch folder for new/deleted .cc files
    auto folder_time = std::filesystem::last_write_time(g_dsp_dir, ec);
    if (!ec && state->folder_modified != std::filesystem::file_time_type{} &&
        folder_time != state->folder_modified) {
      gui::scan_dsp_files(&state->gui_state, g_dsp_dir.string().c_str());
      log_compile("Folder changed, rescanned. Found " +
                  std::to_string(state->gui_state.dsp_files.size()) + " files");
    }
    if (!ec) state->folder_modified = folder_time;

    // Watch selected file for changes
    auto dsp_path = g_dsp_dir / get_selected_dsp_file(state);
    auto mod_time = std::filesystem::last_write_time(dsp_path, ec);
    if (ec)
      return;

    if (state->last_modified != std::filesystem::file_time_type{} &&
        mod_time != state->last_modified) {
      do_recompile(state);
    }
    state->last_modified = mod_time;
  }
}

static const clap_plugin_timer_support_t timer_extension = {
    .on_timer = timer_on_timer,
};

// --- Extension Dispatch ---

static const void *plugin_get_extension(const clap_plugin_t *plugin,
                                        const char *id) {
  (void)plugin;
  if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
    return &audio_ports_extension;
  if (strcmp(id, CLAP_EXT_PARAMS) == 0)
    return &params_extension;
  if (strcmp(id, CLAP_EXT_GUI) == 0)
    return gui::get_extension();
  if (strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
    return &timer_extension;
  return nullptr;
}

static void plugin_on_main_thread(const clap_plugin_t *plugin) {
  (void)plugin;
}

// ============================================================================
// Factory and Entry
// ============================================================================

static uint32_t factory_get_plugin_count(const clap_plugin_factory_t *factory) {
  (void)factory;
  return 1;
}

static const clap_plugin_descriptor_t *
factory_get_plugin_descriptor(const clap_plugin_factory_t *factory,
                              uint32_t index) {
  (void)factory;
  if (index == 0)
    return &plugin_descriptor;
  return nullptr;
}

static const clap_plugin_t *
factory_create_plugin(const clap_plugin_factory_t *factory,
                      const clap_host_t *host, const char *plugin_id) {
  (void)factory;
  if (!clap_version_is_compatible(host->clap_version))
    return nullptr;

  if (strcmp(plugin_id, plugin_descriptor.id) != 0)
    return nullptr;

  auto *state = new PluginState();
  state->host = host;

  auto *plugin = new clap_plugin_t{
      .desc = &plugin_descriptor,
      .plugin_data = state,
      .init = plugin_init,
      .destroy = plugin_destroy,
      .activate = plugin_activate,
      .deactivate = plugin_deactivate,
      .start_processing = plugin_start_processing,
      .stop_processing = plugin_stop_processing,
      .reset = plugin_reset,
      .process = plugin_process,
      .get_extension = plugin_get_extension,
      .on_main_thread = plugin_on_main_thread,
  };

  return plugin;
}

static const clap_plugin_factory_t plugin_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin,
};

/// Entry point initialization - sets up DSP source directory.
static bool entry_init(const char *path) {
  (void)path;

  // Use ~/.local/share/rt-clap
  const char *home = getenv("HOME");
  if (!home)
    return false;
  g_dsp_dir = std::filesystem::path(home) / ".local" / "share" / "rt-clap";

  // Create directory if it doesn't exist
  std::error_code ec;
  std::filesystem::create_directories(g_dsp_dir, ec);

  return true;
}

static void entry_deinit(void) {}

static const void *entry_get_factory(const char *factory_id) {
  if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
    return &plugin_factory;
  return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory,
};
