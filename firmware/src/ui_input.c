/* ui_input.c — panel event extraction (part of the ui.c unity section).
 * Two front-ends, one dispatch surface:
 *   - firmware: fm1_input.h edges (fm1_input_edges / fm1_enc_take /
 *     fm1_input_note_edges), key ids 0..40 (0..13 buttons, 14..40 keys).
 *   - web (ui_web.c): ds_web_* injectors call ui_button/ui_encoder/
 *     panel_key_handler directly with the SAME logical ids (no fm1_in there).
 * Logical ids: buttons = plat_ctrl.h B_* enums; encoders = EN_* enums;
 * panel_key_handler takes key ids 0..26 = notes 53..79 (F3..G5). */

/* firmware front-end (fm1_in statics; compiled only in the unity root) */
static uint32_t panel_held;                      /* key ids we've seen down (release-edge derive) */
#ifndef DS_WEB_UI
static void ui_input_poll(void)
{
    uint32_t pr, re, np;
    uint8_t i;

    pr = fm1_input_edges(&re);
    for (i = 0; i < 14u; i++) {
        uint8_t role = btn_role(i);
        if (role == 0xFF) continue;
        if (pr & (1u << i)) {
            if (role == B_GLO) { glo_held = 1; continue; }
            ui_button(role, 1, 0);
        }
        if (re & (1u << i)) {
            if (role == B_GLO) { glo_held = 0; continue; }
            ui_button(role, 1, 1);
        }
    }
    for (i = 0; i < FM1_NENC; i++) {
        int32_t d = fm1_enc_take(i);
        uint8_t role = enc_role(i);
        while (d > 0) { if (role != 0xFF) ui_encoder(role, 1); d--; }
        while (d < 0) { if (role != 0xFF) ui_encoder(role, -1); d++; }
    }
    np = fm1_input_note_edges();
    for (i = 14u; i < FM1_NKEY; i++) {
        uint8_t held = (uint8_t)((fm1_in.notes >> (i - 14u)) & 1u);   /* debounced mask */
        if (np & (1u << (i - 14u)))
            panel_key_handler(i - 14u, 1);
        else if (!held && panel_held & (1u << (i - 14u)))
            panel_key_up(i - 14u);                /* release edge: was held, now isn't */
        if (np & (1u << (i - 14u)))
            panel_held |= 1u << (i - 14u);
        else if (!(fm1_in.notes & (1u << (i - 14u))))
            panel_held &= ~(1u << (i - 14u));
    }
}
#endif /* DS_WEB_UI */
