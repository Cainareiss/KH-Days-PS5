/* The complete upstream 3D/2D GPU renderer, independent of SDL's window system. */
#ifndef KHDAYS_PS5_VULKAN_GAME_RENDERER_H
#define KHDAYS_PS5_VULKAN_GAME_RENDERER_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct KhdaysFramePackage KhdaysFramePackage;
typedef struct KhdaysNativeVulkanFrame {
    SDL_GPUTexture *upper, *lower;
    int upper_width, upper_height, lower_width, lower_height;
    uint32_t frame;
    /* 0: normal two-screen presentation; 5: one-screen picture;
     * 6: one-display stacked; 7: one-display side-by-side. */
    unsigned output_layout;
} KhdaysNativeVulkanFrame;

int khdays_native_vulkan_renderer_open(SDL_GPUDevice *device);
void khdays_native_vulkan_renderer_close(void);
int khdays_native_vulkan_renderer_draw(const KhdaysFramePackage *package, int scale,
                                      KhdaysNativeVulkanFrame *out);

#endif
