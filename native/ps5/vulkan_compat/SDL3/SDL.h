/* Narrow SDL_GPU source compatibility for the upstream Days compute renderer.
 * Implemented by sdl_gpu_vulkan.c; no SDL window or CPU renderer is linked. */
#ifndef KHDAYS_SDL_GPU_VULKAN_COMPAT_H
#define KHDAYS_SDL_GPU_VULKAN_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
typedef uint8_t Uint8;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUBuffer SDL_GPUBuffer;
typedef struct SDL_GPUTransferBuffer SDL_GPUTransferBuffer;
typedef struct SDL_GPUTexture SDL_GPUTexture;
typedef struct SDL_GPUComputePipeline SDL_GPUComputePipeline;
typedef struct SDL_GPUCommandBuffer SDL_GPUCommandBuffer;
typedef SDL_GPUCommandBuffer SDL_GPUCopyPass;
typedef SDL_GPUCommandBuffer SDL_GPUComputePass;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Event SDL_Event;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef enum SDL_GamepadButton { SDL_GAMEPAD_BUTTON_INVALID = -1 } SDL_GamepadButton;
typedef enum SDL_GamepadType {
    SDL_GAMEPAD_TYPE_UNKNOWN,
    SDL_GAMEPAD_TYPE_XBOX360,
    SDL_GAMEPAD_TYPE_XBOXONE,
    SDL_GAMEPAD_TYPE_PS3,
    SDL_GAMEPAD_TYPE_PS4,
    SDL_GAMEPAD_TYPE_PS5
} SDL_GamepadType;
typedef Uint32 SDL_GPUTextureUsageFlags;
typedef Uint32 SDL_GPUShaderFormat;
typedef enum SDL_GPUTextureFormat {
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM = 1,
    SDL_GPU_TEXTUREFORMAT_R32_UINT = 2
} SDL_GPUTextureFormat;
enum { SDL_GPU_TEXTURETYPE_2D=1, SDL_GPU_SAMPLECOUNT_1=1,
    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ=1,
    SDL_GPU_TEXTUREUSAGE_SAMPLER=1,
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE=2,
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE=4,
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD=1,
    SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD=2,
    SDL_GPU_SHADERFORMAT_SPIRV=1, SDL_GPU_SHADERFORMAT_DXIL=2,
    SDL_GPU_SHADERFORMAT_MSL=4 };
typedef struct SDL_GPUBufferCreateInfo { Uint32 usage,size; } SDL_GPUBufferCreateInfo;
typedef struct SDL_GPUTransferBufferCreateInfo { Uint32 usage,size; } SDL_GPUTransferBufferCreateInfo;
typedef struct SDL_GPUTextureCreateInfo {
    Uint32 type; SDL_GPUTextureFormat format; SDL_GPUTextureUsageFlags usage;
    Uint32 width,height,layer_count_or_depth,num_levels,sample_count;
} SDL_GPUTextureCreateInfo;
typedef struct SDL_GPUComputePipelineCreateInfo {
    const void *code; size_t code_size; const char *entrypoint;
    SDL_GPUShaderFormat format;
    Uint32 num_samplers,num_readonly_storage_textures,num_readonly_storage_buffers;
    Uint32 num_readwrite_storage_textures,num_readwrite_storage_buffers,num_uniform_buffers;
    Uint32 threadcount_x,threadcount_y,threadcount_z;
} SDL_GPUComputePipelineCreateInfo;
typedef struct SDL_GPUTransferBufferLocation { SDL_GPUTransferBuffer *transfer_buffer; Uint32 offset; } SDL_GPUTransferBufferLocation;
typedef struct SDL_GPUBufferRegion { SDL_GPUBuffer *buffer; Uint32 offset,size; } SDL_GPUBufferRegion;
typedef struct SDL_GPUTextureLocation { SDL_GPUTexture *texture; Uint32 mip_level,layer,x,y,z; } SDL_GPUTextureLocation;
typedef struct SDL_GPUTextureRegion { SDL_GPUTexture *texture; Uint32 mip_level,layer,x,y,z,w,h,d; } SDL_GPUTextureRegion;
typedef struct SDL_GPUTextureTransferInfo { SDL_GPUTransferBuffer *transfer_buffer; Uint32 offset,pixels_per_row,rows_per_layer; } SDL_GPUTextureTransferInfo;
typedef struct SDL_GPUStorageTextureReadWriteBinding { SDL_GPUTexture *texture; Uint32 mip_level,layer; bool cycle; } SDL_GPUStorageTextureReadWriteBinding;
typedef struct SDL_GPUStorageBufferReadWriteBinding { SDL_GPUBuffer *buffer; bool cycle; } SDL_GPUStorageBufferReadWriteBinding;
#define SDL_zero(x) memset(&(x),0,sizeof(x))
#define SDL_zeroa(x) memset((x),0,sizeof(x))
#define SDL_zerop(x) memset((x),0,sizeof(*(x)))
#define SDL_arraysize(x) (sizeof(x)/sizeof((x)[0]))
#define SDL_min(a,b) ((a)<(b)?(a):(b))
#define SDL_malloc malloc
#define SDL_realloc realloc
#define SDL_free free
#define SDL_memcpy memcpy
#define SDL_memset memset
#define SDL_getenv getenv
#define SDL_snprintf snprintf
#define SDL_lroundf lroundf
size_t SDL_strlcpy(char *,const char *,size_t);
bool SDL_SetError(const char *,...);
const char *SDL_GetError(void);
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *);
SDL_GPUShaderFormat SDL_GetGPUShaderFormats(SDL_GPUDevice *);
bool SDL_GPUTextureSupportsFormat(SDL_GPUDevice *,SDL_GPUTextureFormat,Uint32,SDL_GPUTextureUsageFlags);
SDL_GPUBuffer *SDL_CreateGPUBuffer(SDL_GPUDevice *,const SDL_GPUBufferCreateInfo *);
void SDL_ReleaseGPUBuffer(SDL_GPUDevice *,SDL_GPUBuffer *);
SDL_GPUTransferBuffer *SDL_CreateGPUTransferBuffer(SDL_GPUDevice *,const SDL_GPUTransferBufferCreateInfo *);
void SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice *,SDL_GPUTransferBuffer *);
void *SDL_MapGPUTransferBuffer(SDL_GPUDevice *,SDL_GPUTransferBuffer *,bool);
void SDL_UnmapGPUTransferBuffer(SDL_GPUDevice *,SDL_GPUTransferBuffer *);
SDL_GPUTexture *SDL_CreateGPUTexture(SDL_GPUDevice *,const SDL_GPUTextureCreateInfo *);
void SDL_ReleaseGPUTexture(SDL_GPUDevice *,SDL_GPUTexture *);
SDL_GPUComputePipeline *SDL_CreateGPUComputePipeline(SDL_GPUDevice *,const SDL_GPUComputePipelineCreateInfo *);
void SDL_ReleaseGPUComputePipeline(SDL_GPUDevice *,SDL_GPUComputePipeline *);
SDL_GPUCommandBuffer *SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *);
bool SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer *);
bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *);
SDL_GPUCopyPass *SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *);
void SDL_EndGPUCopyPass(SDL_GPUCopyPass *);
void SDL_UploadToGPUBuffer(SDL_GPUCopyPass *,const SDL_GPUTransferBufferLocation *,const SDL_GPUBufferRegion *,bool);
void SDL_UploadToGPUTexture(SDL_GPUCopyPass *,const SDL_GPUTextureTransferInfo *,const SDL_GPUTextureRegion *,bool);
void SDL_CopyGPUTextureToTexture(SDL_GPUCopyPass *,const SDL_GPUTextureLocation *,const SDL_GPUTextureLocation *,Uint32,Uint32,Uint32,bool);
SDL_GPUComputePass *SDL_BeginGPUComputePass(SDL_GPUCommandBuffer *,const SDL_GPUStorageTextureReadWriteBinding *,Uint32,const SDL_GPUStorageBufferReadWriteBinding *,Uint32);
void SDL_EndGPUComputePass(SDL_GPUComputePass *);
void SDL_BindGPUComputePipeline(SDL_GPUComputePass *,SDL_GPUComputePipeline *);
void SDL_BindGPUComputeStorageBuffers(SDL_GPUComputePass *,Uint32,SDL_GPUBuffer *const *,Uint32);
void SDL_BindGPUComputeStorageTextures(SDL_GPUComputePass *,Uint32,SDL_GPUTexture *const *,Uint32);
void SDL_PushGPUComputeUniformData(SDL_GPUCommandBuffer *,Uint32,const void *,Uint32);
void SDL_DispatchGPUCompute(SDL_GPUComputePass *,Uint32,Uint32,Uint32);
void SDL_DestroyTexture(SDL_Texture *);
#endif
