# Environment Variables

All environment variables are read at startup (once, cached on first access).
Set them through Winlator's environment / dxvk.conf-wrapping or the wrapper's
config before the app starts.

## Performance
| Variable | Default | Description |
|----------|---------|-------------|
| `WRAPPER_PERF_MODE` | `balanced` | `performance` / `balanced` / `quality`.<br>• `performance`: small BCn textures are decoded on the CPU (great for weak phones like the Helio G99), large ones on the GPU.<br>• `quality`: always use the GPU compute decode (most accurate, slowest). |
| `WRAPPER_BCN_CPU_SIZE` | `262144` | Under `performance`, BCn textures whose area (width*height) is at or below this many pixels are decoded on the CPU. `262144` ≈ 512x512. |
| `WRAPPER_MAX_BCN_SIZE` | `0` (off) | Largest texture dimension in pixels that still gets BCn emulation. Textures larger are left as-is (no decode) and logged once. `0` disables the limit. Useful to skip huge mip/UI splats that are never actually sampled. |

## BCn decode control
| Variable | Default | Description |
|----------|---------|-------------|
| `WRAPPER_BCN_COMPUTE` | `1` | When `0`, force host (CPU) BCn decoding for every texture instead of the GPU compute path. |
| `MASK_BCN` | *empty* | Bitmask / comma list controlling which BCn formats are emulated. Tokens: `common` (BC1/2/3), `alpha` (BC2/3/7), `color` (BC1/4/5/6), `hdr`/`bc6h` (BC6), `rg`/`mono` (BC4/5, i.e. RG/mono), `bc7`, `all`, a hex bitmask (`0x...`, bit = format − 131) or individual format numbers (131..146). |
| `USE_CPU_BCN` | *empty* | Force host CPU BCn decoding for the listed formats (same tokens as `MASK_BCN`). |
| `DISABLE_BCN` | *empty* | Disable BCn emulation entirely for the listed formats (same tokens as `MASK_BCN`). |
| `VALIDATE_BCN` | *empty* | Run the known-good CPU reference decoder against the decode output for the listed formats. |
| `DUMP_BCN` | *empty* | Dump artifacts for the listed formats (implies `VALIDATE_BCN`). |
| `WATERMARK_BCN` | *empty* | Watermark decode output for the listed formats. |
| `WATERMARK_SIZE` | `32` | Watermark tile size in pixels. Sizes: `XS`/`S`/`M`/`L`/`XL`. |

## Logging (HUD)
| Variable | Default | Description |
|----------|---------|-------------|
| `WRAPPER_HUD` | `0` | When `1`, write a rate-limited telemetry CSV to `/sdcard/Documents/Wrapper/hud_*.txt` (fps, decode counters, staging MB, decodes/sec). |
| `WRAPPER_HUD_INTERVAL` | `1.0` | Seconds between HUD samples. |
| `WRAPPER_HUD_FILE` | *empty* | Optional prefix for the HUD file name (default `hud`). |
| `WRAPPER_LOG_LEVEL` | — | Log verbosity for the wrapper log (`/sdcard/Documents/Wrapper/*.txt`). |
| `WRAPPER_CMD_LOG_LEVEL` | — | Log verbosity for command-tracked logs. |

## Presentation
| Variable | Default | Description |
|----------|---------|-------------|
| `WRAPPER_PRESENT_MODE` | *unset* | Wrapper-local override for the surface present mode: `fifo`, `relaxed`, `mailbox`, `immediate`. Applied after `wsi_device_init`, so it wins over `MESA_VK_WSI_PRESENT_MODE`. |

## Driver tuning
| Variable | Default | Description |
|----------|---------|-------------|
| `WRAPPER_REDUCE_DEPTH_FORMAT` | *unset* | `safe` → reduce depth to D16 / D16_S8; `aggressive` → discard stencil; `disabled` → block depth images (debugging). |
| `WRAPPER_EXTENSION_BLACKLIST` | — | List of Vulkan extension names to hide/blacklist. |
| `USe_IMAGE_VIEW` | *unset* | Use image-view-based compute decode path when set (`USE_IMAGE_VIEW`). |

## Legacy / debugging flags
`DEBUG_MEMORY`, `CHECK_FOR_STRIPING`, `WRAPPER_ONE_BY_ONE`, `USE_CPU_BCN`,
`FORCE_/DISABLE_*` (clip distance, spec composite constants, optimization
barriers) are recognized by the wrapper from the upstream build.

> Note: this device's ARM driver does **not** support native BCn
> (`textureCompressionBC = 0`), so every BCn format used at runtime goes
> through the wrapper's emulation paths described above.