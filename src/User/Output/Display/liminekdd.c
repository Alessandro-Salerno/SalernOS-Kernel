#include "USer/Output/Display/liminekdd.h"
#include "kernelpanic.h"
#include "kerntypes.h"
#include "limine.h"

static volatile struct limine_framebuffer_request framebufferRequest = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

static struct limine_framebuffer *mainFramebuffer;

void kernel_liminekdd_init() {
  SOFTASSERT(framebufferRequest.response != NULL, RETVOID);
  SOFTASSERT(framebufferRequest.response->framebuffer_count, RETVOID);

  mainFramebuffer = framebufferRequest.response->framebuffers[0];
}
