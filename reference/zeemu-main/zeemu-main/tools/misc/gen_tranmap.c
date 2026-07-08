// gen_tranmap.c — Generate PrBoom tranmap.dat from DOOM.WAD palette
// Usage: g++ gen_tranmap.c -o gen_tranmap.exe && ./gen_tranmap.exe <doom.wad> <output>
//
// Generates a valid PrBoom cache file (tranmap.dat) with:
//   1 byte: tran_filter_pct (66)
//   768 bytes: PLAYPAL (first palette from WAD)
//   65536 bytes: translucency lookup table (256x256)
// Total: 66305 bytes
//
// NOTE: The Zeebo PrBoom port uses a DIFFERENT cache format:
//   1 byte: pct
//   256 bytes: palette (not 768)
//   65536 bytes: tranmap
// This generator produces the STANDARD PrBoom format.
// The Zeebo port rebuilds the tranmap every time because the
// palette comparison fails (256 vs 768 bytes).

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <climits>

int main(int argc, char* argv[]) {
    const char* wad_path = (argc > 1) ? argv[1] : "D:/Projectos/zeemu/roms/hb/prboom/mod/prboom/doom/DOOM.WAD";
    const char* out_path = (argc > 2) ? argv[2] : "D:/Projectos/zeemu/roms/hb/prboom/mod/prboom/tranmap.dat";

    FILE* wad = fopen(wad_path, "rb");
    if (!wad) { fprintf(stderr, "Cannot open %s\n", wad_path); return 1; }

    // Read WAD header
    char wadtype[5] = {};
    fread(wadtype, 1, 4, wad);
    uint32_t numlumps, infotableofs;
    fread(&numlumps, 4, 1, wad);
    fread(&infotableofs, 4, 1, wad);
    printf("WAD: %s, %u lumps, dir at 0x%x\n", wadtype, numlumps, infotableofs);

    // Find PLAYPAL lump
    fseek(wad, infotableofs, SEEK_SET);
    uint32_t palette_offset = 0;
    for (uint32_t i = 0; i < numlumps; i++) {
        uint32_t filepos, size;
        char name[9] = {};
        fread(&filepos, 4, 1, wad);
        fread(&size, 4, 1, wad);
        fread(name, 1, 8, wad);
        if (strncmp(name, "PLAYPAL", 8) == 0) { palette_offset = filepos; break; }
    }

    if (!palette_offset) { fprintf(stderr, "PLAYPAL not found\n"); fclose(wad); return 1; }

    // Read first palette (768 bytes = 256 * 3 RGB)
    uint8_t playpal[768];
    fseek(wad, palette_offset, SEEK_SET);
    fread(playpal, 1, 768, wad);
    fclose(wad);
    printf("PLAYPAL at 0x%x\n", palette_offset);

    // PrBoom R_InitTranMap algorithm
    unsigned char tran_filter_pct = 66;
    int TSC = 12;
    long w1 = ((unsigned long)tran_filter_pct << TSC) / 100;
    long w2 = (1l << TSC) - w1;

    long pal[3][256], tot[256], pal_w1[3][256];
    for (int i = 0; i < 256; i++) {
        long t, d;
        pal_w1[0][i] = (pal[0][i] = t = playpal[i*3+0]) * w1;
        d = t*t;
        pal_w1[1][i] = (pal[1][i] = t = playpal[i*3+1]) * w1;
        d += t*t;
        pal_w1[2][i] = (pal[2][i] = t = playpal[i*3+2]) * w1;
        d += t*t;
        tot[i] = d << (TSC-1);
    }

    uint8_t tranmap[65536];
    uint8_t* tp = tranmap;
    for (int i = 0; i < 256; i++) {
        long r1 = pal[0][i] * w2;
        long g1 = pal[1][i] * w2;
        long b1 = pal[2][i] * w2;
        for (int j = 0; j < 256; j++, tp++) {
            int color = 255;
            long err;
            long r = pal_w1[0][j] + r1;
            long g = pal_w1[1][j] + g1;
            long b = pal_w1[2][j] + b1;
            long best = LONG_MAX;
            do {
                err = tot[color] - pal[0][color]*r - pal[1][color]*g - pal[2][color]*b;
                if (err < best) { best = err; *tp = color; }
            } while (--color >= 0);
        }
    }

    // Write standard PrBoom tranmap.dat
    FILE* out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "Cannot create %s\n", out_path); return 1; }
    fwrite(&tran_filter_pct, 1, 1, out);
    fwrite(playpal, 1, 768, out);
    fwrite(tranmap, 1, 65536, out);
    fclose(out);
    printf("Generated %s: 66305 bytes (1 + 768 + 65536)\n", out_path);
    return 0;
}
