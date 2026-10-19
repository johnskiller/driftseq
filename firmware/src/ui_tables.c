/* ui_tables.c — drum lane palette + GM tables + key identity (part of ui.c
 * unity section). Lives before ui_glue consumers; shared by all ui files. */
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Drum lanes: NINE instruments (03 §3, John 2026-10-19: "9 差不多够了").
 * The keybed has 11 black keys, so the last 2 stay reserved for a v1.1
 * percussion expansion — pressing them selects nothing rather than erroring
 * (§3: "选不满不报错").
 *
 * Lane ORDER is the 808 panel mental model (kick first, then snare, hats,
 * cymbals, toms), and the lane index — not the MIDI note — is the colour key:
 * GM slot shuffling moves the colour with the instrument. */
#define NDRUM 9
static const uint8_t GM_DRUMS[NDRUM] = {36, 38, 42, 46, 49, 45, 41, 43, 51};
static const char GM_SHORT[NDRUM][5] = {"KCK", "SNR", "CHH", "OHH", "CUP",
                                        "TOM", "RID", "PERC", "SHK"};
/* 11 colours: lanes use the first NDRUM, the rest keep the 16-track hue cycle
 * (HOME hero) from colliding on only 9 hues. */
static const uint16_t D_PAL[11] = {          /* drum lane identity colors (emu DPAL) */
    RGB565(224, 92, 92), RGB565(224, 139, 60), RGB565(217, 201, 74), RGB565(127, 217, 74),
    RGB565(74, 217, 168), RGB565(74, 197, 217), RGB565(92, 127, 224), RGB565(154, 92, 224),
    RGB565(224, 92, 168), RGB565(224, 92, 127), RGB565(201, 217, 107),
};
#define TRACK_HUE(i) D_PAL[(i) % 11]

/* alpha blend two RGB565 colors: out = fg*(255-a)/255 + bg*a/255
 * (a=0 keeps fg, a=255 keeps bg — emu globalAlpha over bg) */
static uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a)
{
    uint32_t r1 = (fg >> 11) & 31u, g1 = (fg >> 5) & 63u, b1 = fg & 31u;
    uint32_t r2 = (bg >> 11) & 31u, g2 = (bg >> 5) & 63u, b2 = bg & 31u;
    return (uint16_t)(((r1 * (255u - a) + r2 * a) / 255u) << 11 |
                      ((g1 * (255u - a) + g2 * a) / 255u) << 5 |
                      ((b1 * (255u - a) + b2 * a) / 255u));
}
