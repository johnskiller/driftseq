/* ui_tables.c — drum lane palette + GM tables + key identity (part of ui.c
 * unity section). Lives before ui_glue consumers; shared by all ui files. */
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

static const uint16_t D_PAL[11] = {          /* drum lane identity colors (emu DPAL) */
    RGB565(224, 92, 92), RGB565(224, 139, 60), RGB565(217, 201, 74), RGB565(127, 217, 74),
    RGB565(74, 217, 168), RGB565(74, 197, 217), RGB565(92, 127, 224), RGB565(154, 92, 224),
    RGB565(224, 92, 168), RGB565(224, 92, 127), RGB565(201, 217, 107),
};
static const uint8_t GM_DRUMS[11] = {36, 38, 42, 46, 49, 45, 41, 43, 51, 47, 39};
static const char GM_SHORT[11][5] = {"KCK", "SNR", "CHH", "OHH", "CUP", "TOM", "RID", "PERC", "SHK", "CAB", "MAR"};
#define TRACK_HUE(i) D_PAL[(i) % 11]
