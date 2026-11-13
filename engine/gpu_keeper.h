// SPDX-License-Identifier: MIT
// 1.7.8 GPU clock keep (see gpu_keeper.cpp). Internal to the engine DLL.
#pragma once
#include <cstdint>
void* cs_keeper_start(uint32_t vendor,uint32_t device);
void cs_keeper_active(void* keeper,bool on);
double cs_keeper_dispatch_ms(void* keeper);
uint64_t cs_keeper_dispatches(void* keeper);
void cs_keeper_stop(void* keeper);
// GPU options set by the plugin before a stream is created (bit 0 clock keep, bit 1 fp16 arithmetic).
int cs_gpu_options_value();
// PCI vendor/device ids of an engine handle (internal).
int cs_engine_ids(void* engine,uint32_t* vendor,uint32_t* device);
