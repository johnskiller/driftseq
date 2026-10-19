/* fm1-ctrl's plat_fm1.c subset: the MIDI ring accessors over usb.c's statics.
 * Derived from x0x's app/plat_fm1.c (GPL-3.0-only, Hügelton Instruments). */
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    mi_r++;
    return 1;
}
void plat_midi_out(uint32_t pkt) { midi_out_event(pkt); }
