/* Execute the real SDL_GPU adapter with strict Vulkan lifetime mocks.
 * This tests API ordering, error propagation and cancellation, not GPU pixels. */
#include "../native/ps5/sdl_gpu_vulkan.c"
#include <assert.h>

static unsigned waits, submits, resets, destroyed_buffers, destroyed_images, copies, blits, waw;
static int pending_transfer, require_pending_wait, fail_descriptors;

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance i, const char *n)
{ (void)i; (void)n; return NULL; }

static VkResult VKAPI_CALL ok_reset_pool(VkDevice d, VkDescriptorPool p, VkDescriptorPoolResetFlags f)
{ (void)d; (void)p; (void)f; return VK_SUCCESS; }
static VkResult VKAPI_CALL reset_command(VkCommandBuffer c, VkCommandBufferResetFlags f)
{ (void)c; (void)f; ++resets; return VK_SUCCESS; }
static VkResult VKAPI_CALL begin(VkCommandBuffer c, const VkCommandBufferBeginInfo *b)
{ (void)c; (void)b; return VK_SUCCESS; }
static VkResult VKAPI_CALL end(VkCommandBuffer c)
{ (void)c; return VK_SUCCESS; }
static VkResult VKAPI_CALL reset_fences(VkDevice d, uint32_t n, const VkFence *f)
{ (void)d; (void)n; (void)f; return VK_SUCCESS; }
static VkResult VKAPI_CALL wait_fences(VkDevice d, uint32_t n, const VkFence *f, VkBool32 a, uint64_t t)
{ (void)d; (void)n; (void)f; (void)a; (void)t; ++waits; return VK_SUCCESS; }
static VkResult VKAPI_CALL submit(VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f)
{
    (void)q; (void)n; (void)s; (void)f;
    if (pending_transfer) assert(destroyed_buffers == 0);
    ++submits;
    return VK_SUCCESS;
}
static void VKAPI_CALL destroy_buffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a)
{
    (void)d; (void)b; (void)a;
    if (pending_transfer) assert(submits && waits);
    ++destroyed_buffers;
}
static void VKAPI_CALL free_memory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a)
{ (void)d; (void)m; (void)a; }
static void VKAPI_CALL destroy_view(VkDevice d, VkImageView v, const VkAllocationCallbacks *a)
{ (void)d; (void)v; (void)a; if (require_pending_wait) assert(waits); }
static void VKAPI_CALL destroy_image(VkDevice d, VkImage i, const VkAllocationCallbacks *a)
{ (void)d; (void)i; (void)a; if (require_pending_wait) assert(waits); ++destroyed_images; }
static void VKAPI_CALL copy_image(VkCommandBuffer c, VkBuffer b, VkImage i, VkImageLayout l,
                                  uint32_t n, const VkBufferImageCopy *r)
{ (void)c; (void)b; (void)i; (void)l; (void)n; (void)r; ++copies; }
static void VKAPI_CALL barrier(VkCommandBuffer c, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                               VkDependencyFlags dep, uint32_t nm, const VkMemoryBarrier *m,
                               uint32_t nb, const VkBufferMemoryBarrier *b,
                               uint32_t ni, const VkImageMemoryBarrier *i)
{
    (void)c; (void)dep; (void)nb; (void)b; (void)ni; (void)i;
    if (nm && src == VK_PIPELINE_STAGE_TRANSFER_BIT && dst == VK_PIPELINE_STAGE_TRANSFER_BIT &&
        m->srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT && m->dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT)
        ++waw;
}
static void VKAPI_CALL blit_image(VkCommandBuffer c, VkImage s, VkImageLayout sl,
                                 VkImage d, VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f)
{
    (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; (void)f;
    assert(waw > blits); ++blits;
}
static VkResult VKAPI_CALL allocate_sets(VkDevice d, const VkDescriptorSetAllocateInfo *a, VkDescriptorSet *s)
{ (void)d; (void)a; (void)s; assert(fail_descriptors); return VK_ERROR_OUT_OF_POOL_MEMORY; }

static SDL_GPUDevice fixture(void)
{
    SDL_GPUDevice d;
    memset(&d, 0, sizeof(d));
    d.device = (VkDevice)(uintptr_t)1;
    d.command = (VkCommandBuffer)(uintptr_t)2;
    d.uniform_coherent = 1;
    d.uniform_alignment = 16;
    return d;
}
static SDL_GPUTexture *texture(void)
{
    SDL_GPUTexture *t = calloc(1, sizeof(*t));
    assert(t); t->width = t->height = 1;
    return t;
}

int main(void)
{
    vkResetDescriptorPool = ok_reset_pool;
    vkResetCommandBuffer = reset_command;
    vkBeginCommandBuffer = begin;
    vkEndCommandBuffer = end;
    vkResetFences = reset_fences;
    vkWaitForFences = wait_fences;
    vkQueueSubmit = submit;
    vkDestroyBuffer = destroy_buffer;
    vkFreeMemory = free_memory;
    vkDestroyImageView = destroy_view;
    vkDestroyImage = destroy_image;
    vkCmdCopyBufferToImage = copy_image;
    vkCmdPipelineBarrier = barrier;
    vkCmdBlitImage = blit_image;
    vkAllocateDescriptorSets = allocate_sets;

    /* Exact upstream backdrop sequence: record upload, release transfer,
     * then submit. Releasing before submit must not destroy its source. */
    SDL_GPUDevice d = fixture();
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(&d);
    SDL_GPUTransferBuffer *upload = calloc(1, sizeof(*upload));
    assert(upload); upload->size = 4;
    SDL_GPUTexture *target = texture();
    SDL_GPUTextureTransferInfo src = {.transfer_buffer = upload};
    SDL_GPUTextureRegion dst = {.texture = target, .w = 1, .h = 1, .d = 1};
    SDL_UploadToGPUTexture(SDL_BeginGPUCopyPass(cmd), &src, &dst, false);
    assert(copies == 1);
    pending_transfer = 1;
    SDL_ReleaseGPUTransferBuffer(&d, upload);
    assert(destroyed_buffers == 0);
    assert(SDL_SubmitGPUCommandBuffer(cmd));
    assert(submits == 1 && waits == 1 && destroyed_buffers == 1);
    pending_transfer = 0;
    SDL_ReleaseGPUTexture(&d, target);

    /* Presentation remains pending when the next frame changes dimensions. */
    d = fixture(); d.pending = 1;
    waits = destroyed_images = 0; require_pending_wait = 1;
    SDL_ReleaseGPUTexture(&d, texture());
    assert(waits == 1 && destroyed_images == 1);
    require_pending_wait = 0;

    /* Cancel must restore touched layout before deferred wrappers are freed. */
    d = fixture(); cmd = SDL_AcquireGPUCommandBuffer(&d);
    target = texture(); target->layout = VK_IMAGE_LAYOUT_GENERAL;
    image_transition(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
    SDL_ReleaseGPUTexture(&d, target);
    const unsigned before = destroyed_images;
    assert(SDL_CancelGPUCommandBuffer(cmd));
    assert(destroyed_images == before + 1 && cmd->touched_count == 0);
    target = texture(); target->layout = VK_IMAGE_LAYOUT_GENERAL;
    cmd = SDL_AcquireGPUCommandBuffer(&d);
    image_transition(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
    assert(SDL_CancelGPUCommandBuffer(cmd));
    assert(target->layout == VK_IMAGE_LAYOUT_GENERAL);
    SDL_ReleaseGPUTexture(&d, target);

    /* A void dispatch that cannot allocate descriptors must poison submit. */
    d = fixture(); cmd = SDL_AcquireGPUCommandBuffer(&d);
    SDL_GPUComputePipeline pipeline; memset(&pipeline, 0, sizeof(pipeline));
    SDL_BindGPUComputePipeline(cmd, &pipeline);
    fail_descriptors = 1;
    SDL_DispatchGPUCompute(cmd, 1, 1, 1);
    const unsigned prior_submits = submits;
    assert(!SDL_SubmitGPUCommandBuffer(cmd));
    assert(strstr(SDL_GetError(), "descriptor allocation failed"));
    assert(submits == prior_submits && !cmd->open);

    /* A failed void bind also survives later calls and blocks submission. */
    cmd = SDL_AcquireGPUCommandBuffer(&d);
    SDL_BindGPUComputeStorageBuffers(cmd, 0, NULL, 1);
    SDL_BindGPUComputePipeline(cmd, &pipeline);
    SDL_DispatchGPUCompute(cmd, 1, 1, 1);
    assert(!SDL_SubmitGPUCommandBuffer(cmd));
    assert(strstr(SDL_GetError(), "storage buffer bind"));
    assert(submits == prior_submits);

    /* Every destination blit establishes ordering over the preceding clear
     * and any overlapping main/PiP blit. */
    target = texture();
    blit(&d, target, (VkImage)(uintptr_t)3, 0, 0, 10, 10);
    blit(&d, target, (VkImage)(uintptr_t)3, 3, 3, 4, 4);
    assert(blits == 2 && waw == 2);
    SDL_ReleaseGPUTexture(&d, target);
    puts("Vulkan lifetime: PASS (record/release/submit, pending release, cancel, rejected dispatch/bind, WAW)");
    return 0;
}
