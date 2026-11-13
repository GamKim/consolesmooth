# Third-party notices

ConsoleSmooth's own code is MIT licensed ([LICENSE](LICENSE)). The full notices that ship with the installer are:

- [engine/THIRD_PARTY_NOTICES.txt](engine/THIRD_PARTY_NOTICES.txt) with complete license texts in [engine/licenses/](engine/licenses/)
- [installer/THIRD_PARTY_NOTICES.txt](installer/THIRD_PARTY_NOTICES.txt) with LGPL/GLib texts in [installer/licenses/](installer/licenses/)
- Pinned upstream commits: [engine/source-manifest.json](engine/source-manifest.json)

| Part | License | In this repository |
|---|---|---|
| rife-ncnn-vulkan (nihui) | MIT | `third_party/rife-ncnn-vulkan/src` — modified, see `third_party/PATCHES/rife-ncnn-vulkan.diff` |
| RIFE 4.6 model (hzwer / rife-ncnn-vulkan) | MIT | `third_party/rife-ncnn-vulkan/models/rife-v4.6`, unmodified, SHA-256 pinned in `engine/model_catalog.h` |
| ncnn (Tencent) | BSD-3-Clause (+ bundled notices) | `third_party/rife-ncnn-vulkan/src/ncnn` — modified, see `third_party/PATCHES/ncnn.diff`; tests, examples, tools, python and docs left out |
| glslang (Khronos) | multi-license, see its LICENSE.txt | inside ncnn; its test data left out |
| AMD FidelityFX SDK 1.1.4 | MIT | `third_party/fidelityfx-sdk-1.1.4` (the parts used); 16-byte alignment fix in ffx_vk.cpp, swapchain stubbed |
| Vulkan-Headers (Khronos) | Apache-2.0 / MIT | `third_party/Vulkan-Headers` (include + licenses) |
| GStreamer 1.18 / GLib | LGPL | not included; used from the user's Streaming Center installation at run time |
| zig 0.14.1 runtime parts (libc++, libunwind, mingw-w64) | see engine/licenses | statically linked by the toolchain; the toolchain itself is downloaded by `tools/Get-Toolchain.ps1` |

No AVerMedia, NVIDIA or other manufacturer binaries are included. AVerMedia does not sponsor or endorse this project.
