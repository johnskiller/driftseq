/* ui_input.c — panel event extraction from fm1_input.h (part of ui.c unity
 * section, lives inside app.c so fm1_in/panel statics are visible).
 * Uses the HAL's official take functions (fm1_input_edges / note_edges /
 * fm1_enc_take) — edges are consumed in batch, then dispatched one-by-one. */

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
        if (np & (1u << (i - 14u)))
            panel_key_handler(i, 1);
    }
}
