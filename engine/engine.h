// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef CS_ENGINE_BUILD
#define CS_API __declspec(dllexport)
#else
#define CS_API __declspec(dllimport)
#endif
// Packed BGR8; dimensions are unchanged. Handles must not be shared concurrently.
CS_API void* cs_create(const wchar_t* model_directory, int gpu_id);
CS_API int cs_interpolate(void* handle, const uint8_t* a, const uint8_t* b,
                         int width, int height, float t, uint8_t* output);
// Immutable source IDs must be unique for the lifetime of this engine.
CS_API int cs_interpolate_tagged(void*,const uint8_t*,const uint8_t*,int,int,float,uint8_t*,uint64_t,uint64_t);
// Same as cs_interpolate_tagged for packed BGRA (4 bytes per pixel, alpha output 255).
CS_API int cs_interpolate_bgra(void*,const uint8_t*,const uint8_t*,int,int,float,uint8_t*,uint64_t,uint64_t);
// 1.0.1: same as cs_interpolate_bgra on a second RIFE network with a fixed flow scale (loaded on first use; RIFE only, else -4).
CS_API int cs_interpolate_bgra_aux(void*,const uint8_t*,const uint8_t*,int,int,float,uint8_t*,uint64_t,uint64_t,float);
CS_API const char* cs_last_error(void);
CS_API const char* cs_gpu_name(void* handle);
CS_API const char* cs_model_name(void* handle);
CS_API double cs_last_ms(void* handle);
// Diagnostic only: GPU fence wait inside the last interpolation, -1 if not measured.
CS_API double cs_last_gpu_wait_ms(void* handle);
// RIFE flow size policy (1.6.5). 1 (default): flow at about FHD (FHD full, QHD 0.75, UHD 0.5);
// 0 (low latency): half-size flow from FHD up (1.6.4 policy, about half the inference time).
CS_API void cs_set_flow_floor(void* handle,int fhd_floor);
// 1.7.8 diagnostics. Stage times of the last interpolation in ms: [0] CPU input copy/convert and command
// recording, [1] submit until the GPU fence, [2] CPU output copy/convert. Returns 1 (values -1) if not measured.
CS_API int cs_last_stages(void* handle,double* out3);
// One line: vendor/device ids, Vulkan API, driver name and version, compute queue family, fp16 support.
CS_API const char* cs_gpu_info(void* handle);
// 1.7.8 settings switches for engines created afterwards: bit 0 keeps the GPU clock up while generating
// (small low-priority compute load; aimed at AMD/Intel), bit 1 RIFE fp16 arithmetic where supported.
CS_API void cs_set_gpu_options(int flags);
CS_API void cs_destroy(void* handle);
// Experimental fixed-cadence stream. Feed every OUTPUT tick; source sampling
// and interpolation phases follow the configured rational rate. One bounded
// pending pair; expired results are bypassed without blocking the caller.
// One caller per handle. Call destroy only after tick calls have stopped.
typedef struct CsStreamStats {
 uint64_t ticks, generated, repeated, static_pairs, cuts, overwritten, failures;
 double last_inference_ms;
 int ready; // 0: loading/full-resolution warm-up, 1: usable, -1: failed
} CsStreamStats;
CS_API void* cs_stream_create(const wchar_t* model_directory,int gpu_id,int width,int height);
CS_API int cs_stream_tick(void* stream,const uint8_t* input,uint8_t* output);
CS_API int cs_stream_stats(void* stream,CsStreamStats* stats);
// On the same thread as tick. Resets temporal history without reloading the GPU.
// latency_mode: 0 low latency, 1 stable. Disabled tick is a byte-exact pass-through.
CS_API int cs_stream_configure(void* stream,int enabled,int source_fps,int latency_mode);
// Arbitrary integer target FPS, source 30/60, source < target <= min(4*source,240).
// One call per negotiated OUTPUT tick. Settings reset history and stale jobs.
// Target cadence is not a guarantee of GPU-generated frames meeting deadlines.
CS_API int cs_stream_configure_rate(void* stream,int enabled,int source_fps,int target_fps,int latency_mode);
CS_API int cs_stream_configure_fraction(void* stream,int enabled,int source_num,int source_den,int target_num,int target_den,int latency_mode);
CS_API uint64_t cs_stream_deadline_misses(void* stream);
CS_API void cs_stream_destroy(void* stream);
// Live capture uses actual arrival time (monotonic nanoseconds), independent of
// output callback count. Identical pixels are held; changed sources are accepted.
typedef struct CsLiveMetrics {
 uint64_t accepted_sources,timestamp_resets,expired_jobs,completed_jobs,discarded_results,deadline_misses,last_source_age_ns,max_source_age_ns;
 int workers;
} CsLiveMetrics;
CS_API void* cs_live_create(const wchar_t*,int,int,int);
CS_API void* cs_live_create_workers(const wchar_t*,int,int,int,int);
// channels: 3 = bgr24 (cs_live_create_workers), 4 = BGRA. Ticks then take w*h*channels bytes.
CS_API void* cs_live_create_format(const wchar_t*,int,int,int,int,int channels);
CS_API int cs_live_tick(void*,const uint8_t*,uint8_t*,uint64_t);
CS_API int cs_live_configure(void*,int,int,int,int,int,int);
CS_API int cs_live_stats(void*,CsStreamStats*);
CS_API int cs_live_metrics(void*,CsLiveMetrics*);
CS_API uint64_t cs_live_deadlines(void*);
// Actual configured buffer; high-resolution stable mode includes warm-up calibration.
CS_API uint64_t cs_live_buffer_ns(void*);
// Explicit buffer override, 25..200ms; invalid values leave state unchanged.
CS_API int cs_live_set_buffer_ms(void*,int);
CS_API int cs_live_set_low_buffer_ms(void*,int);
typedef struct CsStageStats { double in_ms,gpu_ms,out_ms,total_ms,gpu_max_ms,total_max_ms,busy_pct; uint64_t samples; double keep_ms; uint64_t keep_dispatches; } CsStageStats;
// 1.7.8 diagnostics: stage averages over the last 240 inferences (see cs_last_stages). Returns 1 with no samples.
CS_API int cs_live_stage_stats(void*,CsStageStats*);
// Driver/queue line of the first worker's engine (cs_gpu_info); empty until a worker has created its engine.
CS_API const char* cs_live_gpu_info(void*);
CS_API void cs_live_destroy(void*);
// Diagnostic only, call on the producer thread after a generated output.
CS_API int cs_live_copy_proof(void*,uint8_t*,uint8_t*,uint8_t*,float*);
#ifdef __cplusplus
}
#endif
