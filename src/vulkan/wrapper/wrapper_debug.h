#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool check_flag(const char* env, bool default_value);

#define CHECK_FLAG(name) ({ \
    static bool __value; \
    static bool __initialized; \
    if (!__initialized) \
        __value = check_flag(name, false); \
    __value; \
})

uint32_t make_bcn_masks(const char* flag);

uint32_t get_unsupported_bcn_masks(void);

uint32_t get_watermarked_bcn_masks(void);

uint32_t get_host_decoding_bcn_masks(void);

uint32_t get_disabled_bcn_masks(void);

uint32_t get_dump_bcn_masks(void);

uint32_t get_validate_bcn_masks(void);

uint32_t get_dump_src_bcn_masks(void);

uint32_t get_watermark_size(void);

/* Simple performance presets selected with WRAPPER_PERF_MODE */
enum WrapperPerfMode {
   PERF_QUALITY,      /* quality   : always GPU compute BCn decode          */
   PERF_BALANCED,     /* balanced  : default behavior (compute, no auto CPU) */
   PERF_PERFORMANCE,  /* performance: small textures -> CPU, large -> GPU    */
};

enum WrapperPerfMode get_perf_mode(void);

/* WRAPPER_MAX_BCN_SIZE: skip BCn emulation for textures whose largest
 * dimension exceeds this (0 = never skip). */
uint32_t get_max_bcn_dimension(void);

/* WRAPPER_BCN_CPU_SIZE: threshold in pixels under which PERFORMANCE mode
 * decodes BCn on the CPU. Default 262144 (~512x512). */
uint64_t get_bcn_cpu_decode_pixels(void);

/* Log-based HUD (active when WRAPPER_HUD=1). */
bool wrapper_hud_enabled(void);
double wrapper_hud_interval(void);
const char* wrapper_hud_file(void);

void initialize_cmd_print_masks(void);

bool use_image_view_mode(void);

bool use_compute_shader_mode(void);

bool use_wrapper_trace(void);

bool should_log_memory_debug(void);

enum DepthFormatOverrideMode {
   OVERRIDE_NONE,
   OVERRIDE_SAFE, /* Force Drop DXX_SY to D16_UNORM_S8_UINT, DXX to D16_UNORM */
   OVERRIDE_AGGRESSIVE, /* Force D16_UNORM, discard stencil */
   OVERRIDE_DISABLED, /* Disable depth/stencil creation */
};

enum DepthFormatOverrideMode get_depth_format_override_mode(void);

#ifdef __cplusplus
}
#endif
