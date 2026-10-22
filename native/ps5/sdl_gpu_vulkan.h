#ifndef KHDAYS_SDL_GPU_VULKAN_H
#define KHDAYS_SDL_GPU_VULKAN_H
#include <SDL3/SDL.h>
SDL_GPUDevice *khdays_vk_device_open(unsigned width,unsigned height,void (*log)(const char *));
bool khdays_vk_wait_idle(SDL_GPUDevice *device);
void khdays_vk_device_close(SDL_GPUDevice *device);
bool khdays_vk_present(SDL_GPUDevice *device,SDL_GPUTexture *upper,SDL_GPUTexture *lower,unsigned layout);
unsigned khdays_vk_width(SDL_GPUDevice *device);
unsigned khdays_vk_height(SDL_GPUDevice *device);
const char *khdays_vk_error(void);
#endif
