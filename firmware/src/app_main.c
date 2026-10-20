/* app_main.c — DriftSeq boot + main loop (part of app.c unity root).
 * App layer: Copyright (C) 2026 johnskiller, GPL-3.0-only.
 * Boot/ISR skeleton = fm1-ctrl ctrl_main.c (proven on hardware), with the
 * controller's "every control emits" replaced by the sequencer engine:
 * input routes into ui handlers, ds_tick() runs on the 1 ms cadence and
 * its out[] byte stream drains to USB MIDI. */

/* ---- MIDI transport: 3-byte app events <-> USB-MIDI event packets ----
 * Same packing as fm1-ctrl ctrl_main.c: byte0 = cable|CIN (0x9 note, 0xB other
 * channel messages), then status/d1/d2. The ring carries 4-byte packets. */
static void midi_transport_usb3(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t cin = ((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u) ? 0x9u : 0xBu;
    plat_midi_out((uint32_t)cin | (uint32_t)st << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24);
}

/* TIMER5 ISR: fm1-ctrl's (no audio on this firmware) */
static void ds_timer5_irq(void)
{
    static uint32_t sub;
    uint8_t usb_due;
    fm1_timer5_ack();
    fm1_input_tick();
    usb_due = (sub % 5u) == 0u;
    if (usb_due)
        usb_poll();
    if (++sub == 10u)
        sub = 0;
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}

static void isr_timer5(void) { ds_timer5_irq(); }

static void ds_main(void);
extern uint8_t _bss_start[], _bss_end[], _pool_start[], _pool_end[];
extern const uint8_t _data_load[], _data_start[], _data_end[];

void fm1_cstart(void)
{
    uint32_t *d;
    fm1_cpu1_halt();
    fm1_time_init();
    fm1_reset_reason();
    fm1_wdt_arm(0x0D);
    fm1_irq_init();
    for (d = (uint32_t *)_bss_start; d < (uint32_t *)_bss_end; d++)
        *d = 0;
    for (d = (uint32_t *)_pool_start; d < (uint32_t *)_pool_end; d++)
        *d = 0;
    {
        const uint8_t *s;
        for (s = _data_load, d = (uint32_t *)_data_start; (uint8_t *)d < _data_end; s += 4, d++)
            *d = *(const uint32_t *)s;
    }
    fm1_irq_enable_all();
    ds_main();
    for (;;)
        ;
}

/* engine output drain: g.out[] -> transport (01 §7 byte contract; out holds
 * 3-byte messages, out_len counts bytes) */
static void ds_flush_out(void)
{
    uint32_t i;
    for (i = 0; i + 2 < g.out_len; i += 3)
        midi_transport_usb3(g.out[i], g.out[i + 1], g.out[i + 2]);
    g.out_len = 0;
}

/* host -> device (future: emu bridge / LED mirror). v1: drain & ignore. */
static void ds_drain_midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt))
        ;                                            /* v1.1: LED mirror from emu */
}

static void ds_main(void)
{
    uint32_t t0, last_tick;
    lcd_init();
    draw_text_box(0, 96, 240, &FONT_M, "DriftSeq", C_HI, 1);
    draw_text_box(0, 134, 240, &FONT_S, "v1 batch1", C_GRAY, 1);
    fm1_input_init();
    panel_init();
    midi_transport_send = midi_transport_usb3;
    ui_init();
    ds_state_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(300);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_draw();                                       /* first frame */

    t0 = fm1_ms;
    last_tick = fm1_ms;
    for (;;) {
        ui_poll();                                   /* events -> handlers -> redraw */
        if (fm1_ms != last_tick) {                   /* 1 ms engine cadence */
            last_tick = fm1_ms;
            ds_tick(&g, 0.001);
        }
        ui_leds_sync();                              /* UI state -> panel LEDs */
        ds_flush_out();
        ds_drain_midi_in();
        usb_retry(fm1_ms);
        fm1_wdt_feed();
        if (fm1_ms - t0 >= 1000u) {                  /* 1 Hz heartbeat */
            t0 = fm1_ms;
        }
    }
}
