#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/io/log.h>
#include <kernel/com/util.h>
#include <kernel/opt/acpi.h>
#include <kernel/platform/info.h>
#include <vendor/limine.h>

used __attribute__((
    section(".limine_requests"))) static volatile struct limine_memmap_request
    g_memory_map_request = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};

used __attribute__((section(
    ".limine_requests"))) static volatile struct limine_kernel_address_request
    g_kernel_address = (struct limine_kernel_address_request){
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0};

used __attribute__((
    section(".limine_requests"))) static volatile struct limine_rsdp_request
    g_rsdp_request = (struct limine_rsdp_request){.id = LIMINE_RSDP_REQUEST,
                                                  .revision = 0};

used __attribute__((
    section(".limine_requests"))) static volatile struct limine_module_request
    g_module_request = (struct limine_module_request){
        .id = LIMINE_MODULE_REQUEST,
        .revision = 0};

used __attribute__((section(
    ".limine_response"))) static volatile struct limine_framebuffer_request
    g_framebuffer_request = (struct limine_framebuffer_request){
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0};

MemMap *info_get_memmap(void) {
    assert(g_memory_map_request.response != NULL);
    return g_memory_map_request.response;
}

KAddr *info_get_kaddr(void) {
    assert(g_kernel_address.response != NULL);
    return g_kernel_address.response;
}

RSDPInfo *info_get_rsdp(void) {
    assert(g_rsdp_request.response != NULL);
    return g_rsdp_request.response;
}

InfoFile *info_get_initrd(void) {
    assert(g_module_request.response != NULL);
    assert(g_module_request.response->module_count > 0);

    for (u64 i = 0; i < g_module_request.response->module_count; i++) {
        InfoFile *mod = g_module_request.response->modules[i];

        if (strcmp("/initrd", mod->path) == 0) return mod;
    }

    assert(!"no initrd found");
}

Framebuffer *info_get_fb(void) {
    assert(g_framebuffer_request.response != NULL);
    assert(g_framebuffer_request.response->framebuffer_count > 0);

    return g_framebuffer_request.response->framebuffers[0];
}
