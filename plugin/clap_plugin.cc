#include <clap/clap.h>

#include "../src/JIT.h"
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

/// DSP process function signature
using ProcessFn = void (*)(const float *const *, float *const *, uint32_t,
                           uint32_t);

/// Per-instance plugin state
struct PluginState {
  std::unique_ptr<clap_rt::ClapJIT> jit;
  std::atomic<ProcessFn> process_fn{nullptr};
  const clap_host_t *host = nullptr;
  const clap_plugin_t *plugin = nullptr;

  // Hot-reload support (atomic swap at frame boundary)
  std::atomic<bool> reload_pending{false};
  ProcessFn pending_fn = nullptr;

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
  if (auto f = fopen(log_path.c_str(), "a")) {
    fprintf(f, "%s\n", msg.c_str());
    fclose(f);
  }
}

/// Recompiles the DSP code from the selected file in ~/.local/share/rt-clap/.
/// Updates GUI state with success/error status.
/// Uses atomic swap to safely update the process function pointer.
static void do_recompile(PluginState *state) {
  state->gui_state.last_error.clear();
  state->gui_state.compile_success = false;

  auto dsp_path = g_dsp_dir / get_selected_dsp_file(state);
  log_compile("Compiling: " + dsp_path.string());

  // Create fresh JIT instance
  auto jit_or_err = clap_rt::ClapJIT::create();
  if (!jit_or_err) {
    state->gui_state.last_error = llvm::toString(jit_or_err.takeError());
    log_compile("JIT create error: " + state->gui_state.last_error);
    return;
  }

  auto new_jit = std::make_unique<clap_rt::ClapJIT>(std::move(*jit_or_err));

  // Compile DSP code
  auto err = new_jit->addModule(dsp_path.string());
  if (err) {
    state->gui_state.last_error = llvm::toString(std::move(err));
    log_compile("Compile error: " + state->gui_state.last_error);
    return;
  }

  // Lookup process function
  auto fn_or_err = new_jit->lookupAs<void(const float *const *, float *const *,
                                          uint32_t, uint32_t)>("process");
  if (!fn_or_err) {
    state->gui_state.last_error = llvm::toString(fn_or_err.takeError());
    log_compile("Lookup error: " + state->gui_state.last_error);
    return;
  }

  // Set pending function for atomic swap
  state->pending_fn = *fn_or_err;
  state->reload_pending.store(true, std::memory_order_release);

  // Replace JIT instance
  state->jit = std::move(new_jit);
  state->gui_state.compile_success = true;
  log_compile("Compile success!");

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

  // Scan for available DSP files
  gui::scan_dsp_files(&state->gui_state, g_dsp_dir.c_str());
  log_compile("Found " + std::to_string(state->gui_state.dsp_files.size()) + " DSP files");

  // Initialize LLVM (safe to call multiple times)
  clap_rt::ClapJIT::initializeLLVM();

  // Create JIT instance
  auto jit_or_err = clap_rt::ClapJIT::create();
  if (!jit_or_err) {
    log_compile("JIT create error: " + llvm::toString(jit_or_err.takeError()));
    return false;
  }
  state->jit = std::make_unique<clap_rt::ClapJIT>(std::move(*jit_or_err));

  // Compile DSP code
  auto dsp_path = g_dsp_dir / get_selected_dsp_file(state);
  log_compile("Compiling: " + dsp_path.string());
  auto err = state->jit->addModule(dsp_path.string());
  if (err) {
    log_compile("Compile error: " + llvm::toString(std::move(err)));
    return false;
  }

  // Lookup process function
  auto fn_or_err = state->jit->lookupAs<void(const float *const *,
                                             float *const *, uint32_t,
                                             uint32_t)>("process");
  if (!fn_or_err) {
    log_compile("Lookup error: " + llvm::toString(fn_or_err.takeError()));
    return false;
  }
  state->process_fn.store(*fn_or_err, std::memory_order_release);
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
  (void)plugin;
  (void)sample_rate;
  (void)min_frames;
  (void)max_frames;
  return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) {
  (void)plugin;
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
    state->process_fn.store(state->pending_fn, std::memory_order_release);
    state->reload_pending.store(false, std::memory_order_release);
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
      gui::scan_dsp_files(&state->gui_state, g_dsp_dir.c_str());
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
