# Third-party notices

ConsoleSmooth's own code is MIT licensed ([LICENSE](LICENSE)). The full notices that ship with the installer are:

- [engine/THIRD_PARTY_NOTICES.txt](engine/THIRD_PARTY_NOTICES.txt) with complete license texts in [engine/licenses/](engine/licenses/)
- [installer/THIRD_PARTY_NOTICES.txt](installer/THIRD_PARTY_NOTICES.txt) with LGPL/GLib texts in [installer/licenses/](installer/licenses/)
- Pinned upstream commits: [engine/source-manifest.json](engine/source-manifest.json)

| Part | License | In this repository |
|---|---|---|
| rife-ncnn-vulkan (nihui) | MIT | `third_party/rife-ncnn-vulkan/src` — modified, see `third_party/PATCHES/rife-ncnn-vulkan.diff` |
| RIFE 4.6 model (hzwer / rife-ncnn-vulkan) | MIT | `third_party/rife-ncnn-vulkan/models/rife-v4.6`, unmodified, SHA-256 pinned in `engine/model_catalog.h` |
| RIFE 4.15 lite model (hzwer / TNTwise rife-ncnn-vulkan 13338e38) | MIT | `third_party/rife-ncnn-vulkan/models/rife-v4.15-lite`, unmodified, SHA-256 pinned in `engine/model_catalog.h` (2.2.6, optional) |
| ncnn (Tencent) | BSD-3-Clause (+ bundled notices) | `third_party/rife-ncnn-vulkan/src/ncnn` — modified, see `third_party/PATCHES/ncnn.diff`; tests, examples, tools, python and docs left out |
| glslang (Khronos) | multi-license, see its LICENSE.txt | inside ncnn; its test data left out |
| AMD FidelityFX SDK 1.1.4 | MIT | `third_party/fidelityfx-sdk-1.1.4` (the parts used); 16-byte alignment fix in ffx_vk.cpp, swapchain stubbed |
| AMD FidelityFX SDK 2.4.0 frame generation DLLs (`amd_fidelityfx_framegeneration_dx12.dll`, `amd_fidelityfx_loader_dx12.dll`) | AMD binary license (redistribution in binary form with the notice) | installer only (`engine/engines/amdfg`, with `LICENSE.md` and `3rdpartynotice.md`), unmodified, not in this repository |
| NVIDIA DLSS Frame Generation runtime (`nvngx_dlssg.dll` 310.9.16, public NVIDIA Streamline/DLSS SDK) | NVIDIA RTX SDKs license | installer only (`engine/engines/dlssg`, with `LICENSE-NVIDIA-RTX-SDK.txt`), unmodified, not in this repository and not covered by the MIT license. This software uses NVIDIA DLSS. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation. |
| NVIDIA Video Effects SDK runtime (Video Frame Generation) | NVIDIA software license | not included; downloaded by the user from NVIDIA (`Get-NvidiaVfg.ps1`) on first use |
| Vulkan-Headers (Khronos) | Apache-2.0 / MIT | `third_party/Vulkan-Headers` (include + licenses) |
| Vulkan loader (`vulkan-1.dll` 1.4.363.0, LunarG Vulkan Runtime components) | Apache-2.0 / MIT | installer only (`engine/vulkan`, with `VulkanRT-License.txt`), unmodified, SHA-256 pinned in `plugin/vulkan_pin.h`; used only when the graphics driver left no `vulkan-1.dll` (1.1.5) |
| GStreamer 1.18 / GLib | LGPL | not included; used from the user's Streaming Center installation at run time |
| zig 0.14.1 runtime parts (libc++, libunwind, mingw-w64) | see engine/licenses | statically linked by the toolchain; the toolchain itself is downloaded by `tools/Get-Toolchain.ps1` |

No AVerMedia binaries are included. The only prebuilt third-party binaries are the AMD and NVIDIA runtimes and the Vulkan loader listed above, shipped unmodified in the installer under their own licenses. AVerMedia, AMD and NVIDIA do not sponsor or endorse this project.
