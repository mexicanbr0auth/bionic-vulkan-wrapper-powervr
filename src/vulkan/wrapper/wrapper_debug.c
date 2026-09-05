#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "wrapper_private.h"
#include "wrapper_log.h"
#include "wrapper_trampolines.h"
#include "wrapper_debug.h"

bool check_flag(const char* env, bool default_value) {
   const char* value = getenv(env);
   if (!value) return default_value;
   if (strcmp(value, "1") == 0) {
      return true;
   } else if (strcmp(value, "0") == 0) {
      return false;
   } else {
      return default_value;
   }
}

uint32_t make_bcn_masks(const char* flag) {
    uint32_t mask = 0;
    const char* mask_bcn = getenv(flag);
    if (!mask_bcn) return mask;

    if (strstr(mask_bcn, "all")) return 0xffff;

    // Allow an exact hex bitmask: bit (format - 131) = format, e.g.
    // MASK_BCN=0xFFFF enables every BC1..BC7 variant (131..146).
    if (strncmp(mask_bcn, "0x", 2) == 0 || strncmp(mask_bcn, "0X", 2) == 0) {
        return (uint32_t) strtoul(mask_bcn, NULL, 16) & 0xffff;
    }

#define MASK_BIT(format) WLOG("Turning on BCn format " #format " for %s", flag); mask |= 1 << (format - 131)
    if (strstr(mask_bcn, "uncommon")) {
        // 132, 134, 135, 136, 138, 139, 140, 142, 144, 146
        MASK_BIT(132);
        MASK_BIT(134);
        MASK_BIT(135);
        MASK_BIT(136);
        MASK_BIT(138);
        MASK_BIT(139);
        MASK_BIT(140);
        MASK_BIT(142);
        MASK_BIT(144);
        MASK_BIT(146);
    }
    
    if (strstr(mask_bcn, "srgb")) {
        // 132, 134, 135, 136, 138, 146
        MASK_BIT(132);
        MASK_BIT(134);
        MASK_BIT(135);
        MASK_BIT(136);
        MASK_BIT(138);
        MASK_BIT(146);
    }
    if (strstr(mask_bcn, "snorm")) {
        MASK_BIT(140);
        MASK_BIT(142);
    }

    if (strstr(mask_bcn, "common")) {
        // Most common game formats: BC1, BC2, BC3, BC5, BC7
        MASK_BIT(131);
        MASK_BIT(132);
        MASK_BIT(133);
        MASK_BIT(134);
        MASK_BIT(135);
        MASK_BIT(136);
        MASK_BIT(137);
        MASK_BIT(138);
        MASK_BIT(141);
        MASK_BIT(142);
        MASK_BIT(145);
        MASK_BIT(146);
    }
    if (strstr(mask_bcn, "alpha")) {
        // Formats that carry an alpha channel: BC2, BC3, BC7
        MASK_BIT(135);
        MASK_BIT(136);
        MASK_BIT(137);
        MASK_BIT(138);
        MASK_BIT(145);
        MASK_BIT(146);
    }
    if (strstr(mask_bcn, "color")) {
        // BC1 opaque color
        MASK_BIT(131);
        MASK_BIT(132);
        MASK_BIT(133);
        MASK_BIT(134);
    }
    if (strstr(mask_bcn, "hdr") || strstr(mask_bcn, "bc6h")) {
        // BC6H half-float HDR
        MASK_BIT(143);
        MASK_BIT(144);
    }
    if (strstr(mask_bcn, "rg") || strstr(mask_bcn, "mono")) {
        // BC4/BC5 single/two channel
        MASK_BIT(139);
        MASK_BIT(140);
        MASK_BIT(141);
        MASK_BIT(142);
    }

    if (strstr(mask_bcn, "bc1")) {
        MASK_BIT(131);
        MASK_BIT(132);
        MASK_BIT(133);
        MASK_BIT(134);
    }

    if (strstr(mask_bcn, "bc2")) {
        MASK_BIT(135);
        MASK_BIT(136);
    }
    
    if (strstr(mask_bcn, "bc3")) {
        MASK_BIT(137);
        MASK_BIT(138);
    }
    if (strstr(mask_bcn, "bc4")) {
        MASK_BIT(139);
        MASK_BIT(140);
    }
    if (strstr(mask_bcn, "bc5")) {
        MASK_BIT(141);
        MASK_BIT(142);
    }
    if (strstr(mask_bcn, "bc6") || strstr(mask_bcn, "bc6h")) {
        MASK_BIT(143);
        MASK_BIT(144);
    }
    if (strstr(mask_bcn, "bc7")) {
        // Was previously (incorrectly) mapped onto BC2 formats 135/136
        MASK_BIT(145);
        MASK_BIT(146);
    }
#undef MASK_BIT

#define MASK_BIT(format) \
    bool mask_##format = strstr(mask_bcn, #format); \
    if (mask_##format) { \
        WLOG("Turning on BCn format " #format " for %s", flag); \
        mask |= 1 << (format - 131); \
    }

    MASK_BIT(131);
    MASK_BIT(132);
    MASK_BIT(133);
    MASK_BIT(134);
    MASK_BIT(135);
    MASK_BIT(136);
    MASK_BIT(137);
    MASK_BIT(138);
    MASK_BIT(139);
    MASK_BIT(140);
    MASK_BIT(141);
    MASK_BIT(142);
    MASK_BIT(143);
    MASK_BIT(144);
    MASK_BIT(145);
    MASK_BIT(146);
    return mask;
#undef MASK_BIT
}

#define STATIC_INIT(mask, default) \
    static bool initialized = false; \
    static uint32_t mask = default; \
    if (initialized) return mask; \
    initialized = true

uint32_t get_unsupported_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("MASK_BCN");
}

uint32_t get_watermarked_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("WATERMARK_BCN");
}

uint32_t get_watermark_size() {
    STATIC_INIT(size, 32);
    const char* mask_bcn = getenv("WATERMARK_SIZE");
    if (!mask_bcn) return size = 32;

    if (strstr(mask_bcn, "XXL")) return size = 256;
    if (strstr(mask_bcn, "XL")) return size = 128;
    if (strstr(mask_bcn, "L")) return size = 64;
    if (strstr(mask_bcn, "M")) return size = 32;
    if (strstr(mask_bcn, "S")) return size = 16;
    if (strstr(mask_bcn, "XS")) return size = 8;
    if (strstr(mask_bcn, "XXS")) return size = 4;
    return size;
}

uint32_t get_host_decoding_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("USE_CPU_BCN");
}

uint32_t get_disabled_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("DISABLE_BCN");
}

uint32_t get_dump_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("DUMP_BCN");
}

uint32_t get_validate_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("VALIDATE_BCN");
}

uint32_t get_dump_src_bcn_masks() {
    STATIC_INIT(mask, 0);
    return mask = make_bcn_masks("DUMP_SRC_BCN");
}

static void parse_hex_to_struct(struct wrapper_entry_masks *masks, const char *hex_string) {
    uint64_t *fields[16] = {
        &masks->f0, &masks->f1, &masks->f2, &masks->f3, &masks->f4,
        &masks->f5, &masks->f6, &masks->f7, &masks->f8, &masks->f9, 
        &masks->f10, &masks->f11, &masks->f12, &masks->f13, &masks->f14, &masks->f15
    };
    int len = strlen(hex_string);
    const char *ptr = hex_string + len;

    for (int i = 0; i < 16; ++i) {
        if (ptr <= hex_string) {
            break;
        }

        const char *start = ptr - 16;
        if (start < hex_string) {
            start = hex_string;
        }

        char chunk[17]; // 16 chars + null terminator
        int chunk_len = ptr - start;
        strncpy(chunk, start, chunk_len);
        chunk[chunk_len] = '\0';
        *fields[i] = strtoull(chunk, NULL, 16);
        ptr = start;
    }
}

struct wrapper_entry_masks wrapper_printer_masks = { 0 };

void initialize_cmd_print_masks() {
    // set the various bits in wrapper_printer_masks
    const char* mask_bcn = getenv("WRAPPER_CMD_LOG_LEVEL");
    if (!mask_bcn) 
        return;

    if (strcmp(mask_bcn, "all") == 0) {
        wrapper_printer_masks.f0 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f1 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f2 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f3 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f4 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f5 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f6 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f7 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f8 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f9 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f10 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f11 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f12 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f13 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f14 = 0xFFFFFFFFFFFFFFFFULL,
        wrapper_printer_masks.f15 = 0xFFFFFFFFFFFFFFFFULL;
        return;
    }

    if (strstr(mask_bcn, "0x")) {
        // This is a 704-bit hex-encoded integer
        parse_hex_to_struct(&wrapper_printer_masks, mask_bcn + 2);
        return;
    }

#define CMD(call) SET_VK_ID_##call##_ON(wrapper_printer_masks)
    if (strstr(mask_bcn, "gfx")) {
        CMD(CreateShaderModule);
        CMD(DestroyShaderModule);
        CMD(CreatePipelineCache);
        CMD(CreateGraphicsPipelines);
        CMD(DestroyPipeline);
        CMD(CreatePipelineLayout);
        CMD(DestroyPipelineLayout);
        CMD(CreateDescriptorSetLayout);
        CMD(DestroyDescriptorSetLayout);
        CMD(CreateFramebuffer);
        CMD(DestroyFramebuffer);
        CMD(CreateRenderPass);
        CMD(DestroyRenderPass);
        CMD(CmdSetViewport);
        CMD(CmdSetScissor);
        CMD(CmdSetLineWidth);
        CMD(CmdSetDepthBias);
        CMD(CmdSetBlendConstants);
    }

    if (strstr(mask_bcn, "mem")) {
        CMD(GetPhysicalDeviceMemoryProperties);
        CMD(AllocateMemory);
        CMD(MapMemory);
        CMD(BindBufferMemory);
        CMD(GetImageMemoryRequirements);
        CMD(BindImageMemory);
        CMD(CreateBuffer);
        CMD(CreateImage);
        CMD(CreateImageView);
        CMD(GetPhysicalDeviceImageFormatProperties2);
        CMD(GetPhysicalDeviceMemoryProperties2);
        CMD(GetMemoryFdKHR);
        CMD(BindBufferMemory2);
        CMD(GetBufferMemoryRequirements2);
        CMD(GetImageMemoryRequirements2);
        CMD(GetAndroidHardwareBufferPropertiesANDROID);
    }

    if (strstr(mask_bcn, "debug1")) {
        CMD(QueueSubmit);
        CMD(CreateFence);
        CMD(DestroyFence);
        CMD(ResetFences);
        CMD(WaitForFences);
        CMD(CreateBuffer);
        CMD(CreateBufferView);
        CMD(CreateImageView);
        CMD(DestroyImageView);
        CMD(CreateShaderModule);
        CMD(CreateComputePipelines);
        CMD(CreatePipelineLayout);
        CMD(CreateDescriptorSetLayout);
        CMD(DestroyDescriptorPool);
        CMD(UpdateDescriptorSets);
        CMD(ResetCommandPool);
        CMD(AllocateCommandBuffers);
        CMD(BeginCommandBuffer);
        CMD(EndCommandBuffer);
        CMD(CmdBindPipeline);
        CMD(CmdBindDescriptorSets);
        CMD(CmdDispatch);
        CMD(CmdCopyBuffer);
        CMD(CmdCopyImage);
        CMD(CmdCopyBufferToImage);
        CMD(CmdPipelineBarrier);
        CMD(CmdPushConstants);
        CMD(BindBufferMemory2);
        CMD(GetBufferMemoryRequirements2);
    }

    if (strstr(mask_bcn, "bcn")) {
        CMD(QueueSubmit);
        CMD(CreateFence);
        CMD(DestroyFence);
        CMD(ResetFences);
        CMD(WaitForFences);
        CMD(CreateBuffer);
        CMD(CreateBufferView);
        CMD(CreateImageView);
        // CMD(CreateShaderModule);
        CMD(CreateComputePipelines);
        CMD(CreateGraphicsPipelines);
        CMD(CreatePipelineLayout);
        CMD(CreateDescriptorSetLayout);
        CMD(UpdateDescriptorSets);
        CMD(ResetCommandPool);
        CMD(AllocateCommandBuffers);
        CMD(BeginCommandBuffer);
        CMD(EndCommandBuffer);
        CMD(CmdBindPipeline);
        CMD(CmdBindDescriptorSets);
        CMD(CmdDispatch);
        CMD(CmdCopyBuffer);
        CMD(CmdCopyImage);
        CMD(CmdCopyBufferToImage);
        CMD(CmdPipelineBarrier);
        CMD(CmdPushConstants);
        CMD(BindBufferMemory);
        CMD(BindBufferMemory2);
        CMD(BindImageMemory);
        CMD(BindImageMemory2);
        CMD(GetBufferMemoryRequirements2);
        CMD(GetBufferMemoryRequirements);
        CMD(AllocateMemory);
        CMD(MapMemory2KHR);
        CMD(UnmapMemory);
    }

// #define CHECK_CMD_MASK(cmd) \
//     if (strstr(mask_bcn, #cmd)) SET_VK_ID_##cmd##_ON(wrapper_printer_masks);
//     UNROLL_ENTRY_POINTS(CHECK_CMD_MASK)
// #undef CHECK_CMD_MASK
}

static bool g_use_image_view = true;
static bool g_use_compute_shader_mode = true;

bool use_image_view_mode() {
   static bool initialized = false;
   if (initialized) {
      return g_use_image_view;
   }
   initialized = true;

   char* use_image_view_env = getenv("USE_IMAGE_VIEW");
   if (use_image_view_env) {
      if (strcmp(use_image_view_env, "1") == 0) {
         WLOG("Enabling experimental direct imageView mode");
         g_use_image_view = true;
      } else if (strcmp(use_image_view_env, "0") == 0) {
         WLOG("Disabling experimental direct imageView mode");
         g_use_image_view = false;
      }
   }

   return g_use_image_view;
}

bool use_compute_shader_mode() {
    static bool value = true;
    static bool initialized = false;
    if (initialized) {
        return value;
    }
    initialized = true;

    const char* env = getenv("WRAPPER_BCN_COMPUTE");
    if (env) {
        if (strcmp(env, "0") == 0) {
            WLOG("WRAPPER_BCN_COMPUTE=0: using host (CPU) BCn decompression only");
            value = false;
        } else if (strcmp(env, "1") == 0) {
            WLOG("WRAPPER_BCN_COMPUTE=1: using GPU compute-shader BCn decompression");
        }
    }

    return value;
}


bool use_wrapper_trace() {
    static bool value;
    static bool init;
    if (init)
        return value;
    init = true;
    const char* mask_bcn = getenv("WRAPPER_CMD_LOG_LEVEL");
    if (!mask_bcn) 
        return value = false;

    if (strstr(mask_bcn, "trace")) {
        return value = true;
    }

    return value = false;
}

bool should_log_memory_debug() {
    return CHECK_FLAG("DEBUG_MEMORY");
}

enum DepthFormatOverrideMode get_depth_format_override_mode(void) {
    static enum DepthFormatOverrideMode mode = OVERRIDE_NONE;
    static bool initialized = false;
    if (initialized) return mode;
    initialized = true;

    const char* value = getenv("WRAPPER_REDUCE_DEPTH_FORMAT");
    if (!value) return mode;

    if (strcmp(value, "safe") == 0 || strcmp(value, "SAFE") == 0) {
        mode = OVERRIDE_SAFE;
        WLOG("Depth Override: reducing to D16_UNORM/D16_UNORM_S8_UINT");
    } else if (strcmp(value, "aggressive") == 0 || strcmp(value, "AGGRESSIVE") == 0) {
        mode = OVERRIDE_AGGRESSIVE;
        WLOG("Depth Override: reducing to D16_UNORM (discarding stencil)");
    } else if (strcmp(value, "disabled") == 0 || strcmp(value, "DISABLED") == 0) {
        mode = OVERRIDE_DISABLED;
        WLOGE("Depth Override: Disabling depth/stencil images for debugging, expect errors");
    }
    return mode;
}

enum WrapperPerfMode get_perf_mode(void) {
    static enum WrapperPerfMode mode = PERF_BALANCED;
    static bool initialized = false;
    if (initialized) return mode;
    initialized = true;

    const char* value = getenv("WRAPPER_PERF_MODE");
    if (!value) return mode;

    if (strcasecmp(value, "performance") == 0) {
        mode = PERF_PERFORMANCE;
        WLOG("Perf Mode: performance (auto CPU/GPU BCn split, big textures may skip emulation)");
    } else if (strcasecmp(value, "quality") == 0) {
        mode = PERF_QUALITY;
        WLOG("Perf Mode: quality (always GPU compute BCn decode, slowest but most accurate)");
    }
    return mode;
}

uint32_t get_max_bcn_dimension(void) {
    static uint32_t dim = 0;
    static bool initialized = false;
    if (initialized) return dim;
    initialized = true;

    const char* value = getenv("WRAPPER_MAX_BCN_SIZE");
    if (!value) return dim;
    long parsed = strtol(value, NULL, 10);
    if (parsed <= 0) return dim;
    dim = (uint32_t) parsed;
    WLOG("Max BCn size = %u px (textures larger than this skip emulation)", dim);
    return dim;
}

uint64_t get_bcn_cpu_decode_pixels(void) {
    static uint64_t px = 262144;
    static bool initialized = false;
    if (initialized) return px;
    initialized = true;

    const char* value = getenv("WRAPPER_BCN_CPU_SIZE");
    if (!value) return px;
    long parsed = strtol(value, NULL, 10);
    if (parsed > 0) px = (uint64_t) parsed;
    return px;
}

bool wrapper_hud_enabled(void) {
    return CHECK_FLAG("WRAPPER_HUD");
}

double wrapper_hud_interval(void) {
    static double interval = 1.0;
    static bool initialized = false;
    if (initialized) return interval;
    initialized = true;

    const char* value = getenv("WRAPPER_HUD_INTERVAL");
    if (!value) return interval;
    double parsed = strtod(value, NULL);
    if (parsed >= 0.1) interval = parsed;
    return interval;
}

const char* wrapper_hud_file(void) {
    return getenv("WRAPPER_HUD_FILE");
}
