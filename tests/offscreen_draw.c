/* Test offscreen drawing via IDisplay_SetDestination and IImage_Draw */
#include <assert.h>
#include "../src/brew/boot.c"
#include "../src/brew/iimage.h"
#include "../src/gpu/framebuffer.h"

int main(void) {
    assert(zmem_init());
    zheap_reset();
    /* boot core */
    zboot_start(0x1000, 0x0102F789u);
    g_state = BOOT_RUNNING;

    /* Create a bitmap destination (IDIB) */
    g_cpu.r[0] = 100; // width
    g_cpu.r[1] = 100; // height
    g_cpu.r[2] = 16;  // depth
    zbrew_handle_idisplay_real(IDISPLAY_TRAP_BASE + 8); // CreateDIBitmap
    uint32_t bitmap = g_cpu.r[0];
    assert(bitmap);

    /* Set destination to the bitmap */
    g_cpu.r[1] = bitmap;
    zbrew_handle_idisplay_real(IDISPLAY_TRAP_BASE + 14); // SetDestination

    /* Create an IImage (simple 1x1 red pixel) */
    const uint8_t ppm[] = "P6\n1 1\n255\n\xff\0\0";
    uint32_t img = zimage_create_from_buffer(AEECLSID_CPNG, ppm, sizeof ppm - 1, "red");
    assert(img);

    /* Set destination on IImage (optional) */
    g_cpu.r[0] = img; g_cpu.r[1] = bitmap; g_cpu.r[2] = 0; g_cpu.r[3] = 0;
    zbrew_handle_idisplay_real(IDISPLAY_TRAP_BASE + 15); // GetDestination (just for sanity)

    /* Draw at (0,0) */
    g_cpu.r[0] = img; g_cpu.r[1] = 0; g_cpu.r[2] = 0; // Draw(x=0,y=0)
    zimage_handle(ZT_IMAGE_BASE + 2);

    /* Verify device framebuffer unchanged (still black) */
    assert(zfb_init());
    uint32_t pixel = zfb_get_pixel(0,0);
    assert(pixel == 0x00000000u);
    return 0;
}
