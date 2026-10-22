/* Host preview of the unmodified official Muse procedural renderer. */
#include <stdio.h>
#include "muse_pixel.h"
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 3;
    uint16_t pixels[64 * 64];
    muse_pixel_set_size(64);
    for (int mode = 0; mode < MUSE_MODE_COUNT; mode++) {
        for (int frame = 0; frame < 24; frame++) {
            muse_pose_t pose = {.mode = mode, .t = 5.0f + frame / 12.0f,
                .mode_t = frame / 12.0f, .level = 0.55f, .happy = 0};
            muse_pixel_render(&pose);
            muse_pixel_scale(pixels, 64, 0, 63, 0, 63);
            if (fwrite(pixels, sizeof(pixels), 1, f) != 1) return 4;
        }
    }
    return fclose(f) == 0 ? 0 : 5;
}
