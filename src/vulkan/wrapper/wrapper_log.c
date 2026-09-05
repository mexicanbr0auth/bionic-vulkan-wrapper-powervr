#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "wrapper_private.h"
#include "wrapper_log.h"
#include "wrapper_trampolines.h"
#include "wrapper_entrypoints.h"
#include "vk_printers.h"

static int __log_level;
static FILE* __log_fd;
static bool __log_initialized;
static double __start_ms;

FILE* get_wrapper_log_fd() {
    return __log_fd;
}

static int __cmd_log_level;
static FILE* __cmd_log_fd;
static bool __cmd_log_initialized;

FILE* get_wrapper_cmd_log_fd() {
    return __cmd_log_fd;
}

void get_current_time_string(char* buffer, size_t bufferSize);
static void ensure_log_dir(void);

static void cleanup_log_file(void) {
    if (__log_fd) {
        fclose(__log_fd);
        __log_fd = NULL;
    }
}

static void cleanup_cmd_log_file(void) {
    if (__cmd_log_fd) {
        fclose(__cmd_log_fd);
        __cmd_log_fd = NULL;
    }
}

static FILE* __hud_fd;
static bool __hud_initialized;

FILE* wrapper_get_hud_fd(void) {
    if (!__hud_initialized) {
        __hud_initialized = true;
        if (wrapper_hud_enabled()) {
            const char* base = wrapper_hud_file();
            char time_str[20];
            get_current_time_string(time_str, sizeof(time_str));
            char path[256];
            ensure_log_dir();
            if (base) {
                snprintf(path, sizeof(path), "/sdcard/Documents/Wrapper/%s_%s.%s.%d.txt",
                         base, time_str, getprogname(), getpid());
            } else {
                snprintf(path, sizeof(path), "/sdcard/Documents/Wrapper/hud_%s.%s.%d.txt",
                         time_str, getprogname(), getpid());
            }
            __hud_fd = fopen(path, "w");
            if (__hud_fd) {
                atexit(wrapper_hud_cleanup);
                fprintf(__hud_fd,
                    "# fps  bcn_images  bcn_decodes  host_decodes  skips  staging_MB  bcn_per_s\n");
                fflush(__hud_fd);
            }
            LOG("HUD enabled -> %s (%s)", path, __hud_fd ? "ok" : "failed");
        }
    }
    return __hud_fd;
}

void wrapper_hud_cleanup(void) {
    if (__hud_fd) {
        fclose(__hud_fd);
        __hud_fd = NULL;
    }
}

void get_current_time_string(char* buffer, size_t bufferSize) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info == NULL) {
        buffer[0] = '\0';
        return;
    }
    strftime(buffer, bufferSize, "%Y_%m_%d_%H_%M_%S", tm_info);
}

static double get_current_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

static void ensure_log_dir(void) {
    static bool created = false;
    if (created) return;
    mkdir("/sdcard/Documents/Wrapper", S_IRWXU | S_IRWXG | S_IRWXO);
    created = true;
}

static FILE* open_log_file(const char* prefix) {
    char time_str[20];
    get_current_time_string(time_str, sizeof(time_str));
    char path[256];
    ensure_log_dir();
    sprintf(path, "/sdcard/Documents/Wrapper/%s_%s.%s.%d.txt", prefix, time_str, getprogname(), getpid());
    return fopen(path, "a");
}

static volatile sig_atomic_t __crash_handler_installed = 0;
static volatile sig_atomic_t __in_crash_handler = 0;
static struct sigaction __prev_sigsegv;

static void write_all(int fd, const char* data, size_t len) {
    while (len > 0) {
        ssize_t written = write(fd, data, len);
        if (written <= 0) return;
        data += written;
        len -= written;
    }
}

static void write_str(int fd, const char* s) {
    write_all(fd, s, strlen(s));
}

static uintptr_t read_pc_from_ctx(void* context) {
    ucontext_t* uc = (ucontext_t*) context;
#if defined(__aarch64__)
    return uc->uc_mcontext.pc;
#else
    return 0;
#endif
}

#if defined(__aarch64__)
static void write_backtrace(int out_fd, uintptr_t pc, uintptr_t lr, uintptr_t fp) {
    char line[128];

    // First frame: faulting pc and its return address (x30 alias).
    int n = snprintf(line, sizeof(line), "  #00 pc=%p lr=%p\n",
                     (void*)pc, (void*)lr);
    write_all(out_fd, line, (size_t)n);

    // Walk the AAPCS64 frame-pointer chain (x29). Frames grow from high to
    // low addresses, so each saved x29 must point strictly UP the stack.
    uintptr_t cur = fp;
    for (int i = 1; i < 32; i++) {
        if (cur < 0x100000ULL || cur > 0x800000000000ULL || (cur & 7) != 0)
            break;
        volatile uint64_t* frame = (volatile uint64_t*)cur;
        uintptr_t next_fp = (uintptr_t)frame[0];
        uintptr_t ret    = (uintptr_t)frame[1];
        if (next_fp <= cur || ret == 0)
            break;
        n = snprintf(line, sizeof(line), "  #%02d pc=%p\n",
                     i, (void*)ret);
        write_all(out_fd, line, (size_t)n);
        cur = next_fp;
    }
}
#endif

static void crash_handler(int signo, siginfo_t* info, void* context) {
    FILE* fd = __log_fd;
    int raw_fd = fd ? fileno(fd) : -1;
    if (raw_fd < 0) {
        raw_fd = STDERR_FILENO;
    }

    if (__in_crash_handler) {
        uintptr_t pc = 0, sp = 0, lr = 0;
        ucontext_t* uc = (ucontext_t*) context;
#if defined(__aarch64__)
        pc = uc->uc_mcontext.pc;
        sp = uc->uc_mcontext.sp;
        lr = uc->uc_mcontext.regs[30];
#endif
        char rb[256];
        int n = snprintf(rb, sizeof(rb),
            "CRASH: re-entered crash handler (signal %d, si_addr=%p, pc=%p, lr=%p, sp=%p), aborting\n",
            signo, info ? info->si_addr : NULL,
            (void*)pc, (void*)lr, (void*)sp);
        write_all(raw_fd, rb, n);
        signal(signo, SIG_DFL);
        raise(signo);
        _exit(128 + signo);
    }
    __in_crash_handler = 1;

    // Chain to a previously installed SIGSEGV handler (e.g. FEX's ARM64EC
    // guard-page trap handler). If it resolves the trap it rewrites the
    // ucontext (pc) and returns; in that case this is NOT a real crash and we
    // must not dump/re-raise. Without chaining we clobber FEX's handler and
    // every legitimate translation trap dies in here.
    if (signo == SIGSEGV &&
        __prev_sigsegv.sa_handler != SIG_DFL &&
        __prev_sigsegv.sa_handler != SIG_IGN) {
        uintptr_t pc_before = read_pc_from_ctx(context);
        if (__prev_sigsegv.sa_flags & SA_SIGINFO) {
            __prev_sigsegv.sa_sigaction(signo, info, context);
        } else {
            __prev_sigsegv.sa_handler(signo);
        }
        uintptr_t pc_after = read_pc_from_ctx(context);
        if (pc_after != pc_before) {
            __in_crash_handler = 0;
            return;
        }
    }

    char buf[256];
    uintptr_t pc = 0, lr = 0, sp = 0, fp = 0;
    ucontext_t* uc = (ucontext_t*) context;
#if defined(__aarch64__)
    pc = uc->uc_mcontext.pc;
    sp = uc->uc_mcontext.sp;
    lr = uc->uc_mcontext.regs[30];
    fp = uc->uc_mcontext.regs[29];
#endif

    int n = snprintf(buf, sizeof(buf),
        "\n===== CRASH (signal %d, si_addr=%p, pid=%d) =====\n",
        signo, info ? info->si_addr : NULL, getpid());
    write_all(raw_fd, buf, n);

    n = snprintf(buf, sizeof(buf),
        "  pc=%p  lr=%p  sp=%p  fp=%p\n",
        (void*)pc, (void*)lr, (void*)sp, (void*)fp);
    write_all(raw_fd, buf, n);

    char marker_path[256];
    get_current_time_string(buf, sizeof(buf));
    snprintf(marker_path, sizeof(marker_path),
        "/sdcard/Documents/Wrapper/crash_%s_%s.%d.txt",
        buf, getprogname(), (int)getpid());
    int mfd = open(marker_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (mfd >= 0) {
        int mn = snprintf(buf, sizeof(buf),
            "CRASH signal %d, si_addr=%p, pid=%d\n",
            signo, info ? info->si_addr : NULL, getpid());
        write_all(mfd, buf, mn);
        mn = snprintf(buf, sizeof(buf), "pc=%p lr=%p sp=%p fp=%p\n",
            (void*)pc, (void*)lr, (void*)sp, (void*)fp);
        write_all(mfd, buf, mn);

        write_str(mfd, "=== /proc/self/maps ===\n");
        int maps_fd = open("/proc/self/maps", O_RDONLY);
        if (maps_fd >= 0) {
            char mbuf[2048];
            ssize_t r;
            while ((r = read(maps_fd, mbuf, sizeof(mbuf))) > 0) {
                write_all(mfd, mbuf, (size_t)r);
            }
            close(maps_fd);
        }
        write_str(mfd, "=== end maps ===\n");
#if defined(__aarch64__)
        write_str(mfd, "=== backtrace ===\n");
        write_backtrace(mfd, pc, lr, fp);
        write_str(mfd, "=== end backtrace ===\n");
#endif
        close(mfd);
    }

    write_str(raw_fd, "\n");
    fflush(fd);

    __in_crash_handler = 0;
    signal(signo, SIG_DFL);
    raise(signo);
    _exit(128 + signo);
}

static void install_crash_handler(void) {
    if (__crash_handler_installed) {
        return;
    }
    __crash_handler_installed = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, NULL, &__prev_sigsegv);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

int should_log_cmd() {
    if (__cmd_log_initialized) {
        return __cmd_log_level;
    }
    __cmd_log_initialized = true;

    const char *log_level = getenv("WRAPPER_CMD_LOG_LEVEL");
    LOG("Logging cmds at %s", log_level);
    if (!log_level) {
        __cmd_log_level = VK_CMD_NONE;
    } else if (strcmp(log_level, "all") == 0) {
        __cmd_log_level = VK_CMD_ALL;
    } else if (strcmp(log_level, "name") == 0) {
        __cmd_log_level = VK_CMD_NAME;
    } else if (strcmp(log_level, "none") == 0) {
        __cmd_log_level = VK_CMD_NONE;
    } else {
        __cmd_log_level = VK_CMD_ALL;
    }

    if (__cmd_log_level != LOG_LEVEL_NONE) {
        __cmd_log_fd = open_log_file("wrapper_cmds");
        if (!__cmd_log_fd) {
            // Try to log to stdout / winelogs
            __cmd_log_fd = stdout;
        } else {
            atexit(cleanup_cmd_log_file);
        }
    }

    return __cmd_log_level;
}

int should_log() {
    if (__log_initialized) {
        return __log_level;
    }
    __log_initialized = true;

    __start_ms = get_current_seconds();

    const char *log_level = getenv("WRAPPER_LOG_LEVEL");
    LOG("Logging logs at %s", log_level);
    if (!log_level) {
        __log_level = LOG_LEVEL_ERROR;
    } else if (strcmp(log_level, "all") == 0) {
        __log_level = LOG_LEVEL_ALL;
    } else if (strcmp(log_level, "trace") == 0) {
        __log_level = LOG_LEVEL_TRACE;
    } else if (strcmp(log_level, "debug") == 0) {
        __log_level = LOG_LEVEL_DEBUG;
    } else if (strcmp(log_level, "verbose") == 0) {
        __log_level = LOG_LEVEL_VERBOSE;
    } else if (strcmp(log_level, "error") == 0) {
        __log_level = LOG_LEVEL_ERROR;
    } else {
        __log_level = LOG_LEVEL_NONE;
    }

    if (__log_level != LOG_LEVEL_NONE) {
        __log_fd = open_log_file("wrapper_log");
        // If __log_fd failed to open, still log to logcat
        if (!__log_fd) {
            // Try to log to stdout / winelogs
            __log_fd = stdout;
        } else {
            atexit(cleanup_log_file);
        }
        install_crash_handler();
    }

    return __log_level;
}

void wlog(const char* fmt, ...) {
    FILE* fd = __log_fd;
    if (!fd) {
        return;
    }
    static simple_mtx_t wlog_mutex = SIMPLE_MTX_INITIALIZER;
    simple_mtx_lock(&wlog_mutex);
    fprintf(fd, "[%06.2f] ", (get_current_seconds() - __start_ms));
    va_list args;
    va_start(args, fmt);
    vfprintf(fd, fmt, args);
    va_end(args);
    fprintf(fd, "\n");
    fflush(fd);
    simple_mtx_unlock(&wlog_mutex);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
wrapper_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageType,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData)
{
    LOG("[Mesa]: %s", pCallbackData->pMessage);
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            WLOGD("[Mesa] %s", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            WLOG("[Mesa] %s", pCallbackData->pMessage);
            break;
        default:
            WLOGE("[Mesa] %s", pCallbackData->pMessage);
            break;
    }
    return VK_FALSE;
}


static void print_vendor_fault_binary(uint32_t size, const void* dump) {
    const uint32_t* pQuads = (const uint32_t*) dump;
    const uint8_t* pChars = (const uint8_t*) dump;
    if (size == 0 || !dump) {
        return;
    }
    int i = 0;
    while (i < size) {
#define QUAD(n) pQuads[(i / 4) + n]
#define UNROLL(bytes, fmt, ...) \
        if (i + bytes - 1 < size) { \
            WLOGE("  .dump(%p)[%04x]:" fmt, dump, i, __VA_ARGS__); \
            i += bytes; \
            continue; \
        }

        UNROLL(32, "%08x %08x %08x %08x", QUAD(0), QUAD(1), QUAD(2), QUAD(3));
        UNROLL(24, "%08x %08x %08x", QUAD(0), QUAD(1), QUAD(2));
        UNROLL(16, "%08x %08x", QUAD(0), QUAD(1));
        UNROLL(8, "%08x", QUAD(0));

        if (i % 8 == 0) {
            WLOGE("  .dump(%p)[%04x]: ", dump, i);
        }
        WLOGE("      %02x", pChars[i]);
        i++;
#undef QUAD
#undef UNROLL
    }
}

void wrapper_log_device_fault(struct wrapper_device *device, const char* call)
{
    WLOGE("FATAL: %s failed with a GPU fault, the game will now crash", call);

    if (!device || !device->physical->base_supported_extensions.EXT_device_fault) {
        WLOGE("+ VK_EXT_device_fault not supported.");
        return;
    }

    if (!device->physical->base_supported_features.deviceFault) {
        WLOGE("+ VK_EXT_device_fault supported, but the 'deviceFault' feature was not enabled.");
        return;
    }

    VkDeviceFaultCountsEXT fault_counts = { 0 };
    fault_counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    VkResult result = WCHECK(GetDeviceFaultInfoEXT((VkDevice)device, &fault_counts, NULL));
    if (result == VK_INCOMPLETE) {
        WLOGE("WARN: Got an incomplete from GetDeviceFaultInfoEXT");
    }

    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        WLOGE("Failed to get the fault_counts from GetDeviceFaultInfoEXT");
        return;
    }

    WLOGE("Fault counts: addressInfoCount=%d, vendorInfoCount=%d, vendorBinarySize=%d",
        fault_counts.addressInfoCount, fault_counts.vendorInfoCount, fault_counts.vendorBinarySize);

    if (fault_counts.addressInfoCount == 0 &&
        fault_counts.vendorInfoCount == 0 &&
        fault_counts.vendorBinarySize == 0) {
        WLOGE("+ Device lost, but no fault info was recorded by the driver.");
        return;
    }

    const bool vendor_binary_feature_enabled =
        device->physical->base_supported_features.deviceFaultVendorBinary;
    if (!device->physical->base_supported_features.deviceFaultVendorBinary) {
        WLOGE("+ deviceFaultVendorBinary feature not available, cannot dump vendor specific fault info");
    }

    VkDeviceFaultInfoEXT fault_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
    };

    fault_info.pAddressInfos = NULL;
    fault_info.pVendorInfos = NULL;
    fault_info.pVendorBinaryData = NULL;

    if (fault_counts.addressInfoCount > 0) {
        fault_info.pAddressInfos = VK_ALLOC2(device, VkDeviceFaultAddressInfoEXT,
                                             fault_counts.addressInfoCount * sizeof(VkDeviceFaultAddressInfoEXT));
    }

    if (fault_counts.vendorInfoCount > 0) {
        fault_info.pVendorInfos = VK_ALLOC2(device, VkDeviceFaultVendorInfoEXT,
                                            fault_counts.vendorInfoCount * sizeof(VkDeviceFaultVendorInfoEXT));
    }

    if (vendor_binary_feature_enabled && fault_counts.vendorBinarySize > 0) {
        fault_info.pVendorBinaryData = VK_ALLOC2(device, uint8_t, fault_counts.vendorBinarySize);
    }

    result = WCHECK(GetDeviceFaultInfoEXT((VkDevice)device, &fault_counts, &fault_info));
    if (result == VK_SUCCESS) {
        WLOGE("--- VULKAN DEVICE FAULT DETECTED ---");
        WLOGE("Description: %s", fault_info.description);
        for (uint32_t i = 0; i < fault_counts.addressInfoCount; i++) {
            WLOGE(".pAddressInfos[%d]", i);
            LOG_STRUCT_AT(ERROR, VkDeviceFaultAddressInfoEXT, &fault_info.pAddressInfos[i]);
        }
        for (uint32_t i = 0; i < fault_counts.vendorInfoCount; i++) {
            WLOGE(".pVendorInfos[%d]", i);
            LOG_STRUCT_AT(ERROR, VkDeviceFaultVendorInfoEXT, &fault_info.pVendorInfos[i]);
        }
        if (fault_info.pVendorBinaryData && fault_counts.vendorBinarySize > 0) {
            WLOGE("  Vendor binary crash dump retrieved (%llu bytes).", fault_counts.vendorBinarySize);
            print_vendor_fault_binary(fault_counts.vendorBinarySize, fault_info.pVendorBinaryData);
        }
        WLOGE("--- END FAULT INFO ---");
    } else {
        WLOGE("Failed to get the fault_info from GetDeviceFaultInfoEXT");
    }

    if (fault_info.pAddressInfos)
        vk_free(&device->vk.alloc, fault_info.pAddressInfos);
    if (fault_info.pVendorInfos)
        vk_free(&device->vk.alloc, fault_info.pVendorInfos);
    if (fault_info.pVendorBinaryData)
        vk_free(&device->vk.alloc, fault_info.pVendorBinaryData);
}
