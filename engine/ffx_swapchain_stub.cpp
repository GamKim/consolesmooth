// SPDX-License-Identifier: MIT
// The vendored FidelityFX VK backend references its swapchain hook; ConsoleSmooth never presents
// through FidelityFX (frames go back to Streaming Center), so the swapchain sources are not built.
#include <FidelityFX/host/backends/vk/ffx_vk.h>
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*){return FFX_ERROR_INVALID_ARGUMENT;}
