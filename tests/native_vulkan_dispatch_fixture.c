/* Reproduce Mesa's weak references: a lazy archive does not supply the
 * callback, but a whole-archive link retains the real function and pointer. */
#if defined(FIXTURE_IMPLEMENTATION)
void vk_common_GetPhysicalDeviceProperties2(void) {}
#else
extern void vk_common_GetPhysicalDeviceProperties2(void) __attribute__((weak));
void (*const vk_common_physical_device_entrypoints[])(void) = {
    vk_common_GetPhysicalDeviceProperties2
};
int main(void)
{
    return vk_common_physical_device_entrypoints[0] != 0 ? 0 : 1;
}
#endif
