/* ui_text.c — tiny string builders (no printf in the Felucca platform; it has
 * fmt_int/fmt_fix only). Part of ui.c unity section. Every builder returns a
 * pointer into a small static ring of buffers (calls within one frame are few). */
static char txt_pool[8][36];
static uint8_t txt_i;

static char *txt_next(void)
{
    char *p = txt_pool[txt_i];
    txt_i = (uint8_t)((txt_i + 1) & 7);
    return p;
}

static void strcpy_(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static char *txt_end(char *b)
{
    while (*b) b++;
    return b;
}

/* "T%d" — track number part */
static char *txt_trk(const char *prefix, uint8_t one_based)
{
    char *b = txt_next();
    strcpy_(b, prefix);
    fmt_int(txt_end(b), one_based);
    return b;
}

/* "st%d" — 1-based step (rev.9 display convention) */
static char *txt_st(uint8_t one_based)
{
    return txt_trk("ST", one_based);
}

/* txt_join with an optional tail — NULL tail returns just a */
static char *txt_join2(const char *a, const char *b)
{
    if (!b)
        return txt_join(a, "");
    return txt_join(a, b);
}

/* "KCK st17" — join with a space */
static char *txt_join(const char *a, const char *b)
{
    char *w = txt_next();
    strcpy_(w, a);
    strcpy_(txt_end(w), " ");
    strcpy_(txt_end(w), b);
    return w;
}

/* "V127 G4 P100%" — drum readout params (r16e) */
static char *txt_vgp(uint8_t vel, uint8_t gate_raw, uint8_t prob)
{
    char *w = txt_next();
    strcpy_(w, "V");
    fmt_int(txt_end(w), vel);
    strcpy_(txt_end(w), " G");
    fmt_int(txt_end(w), gate_raw);
    strcpy_(txt_end(w), " P");
    fmt_int(txt_end(w), prob);
    strcpy_(txt_end(w), "%");
    return w;
}

/* "K 4  N 16  ROT 0" — ARP number row */
static char *txt_knr(uint8_t k, uint8_t n, uint8_t rot)
{
    char *w = txt_next();
    strcpy_(w, "K ");
    fmt_int(txt_end(w), k);
    strcpy_(txt_end(w), "  N ");
    fmt_int(txt_end(w), n);
    strcpy_(txt_end(w), "  ROT ");
    fmt_int(txt_end(w), rot);
    return w;
}

/* number -> buffer (optionally after a prefix); fmt_int returns void so this
 * wrapper makes inline use in cv_text() calls ergonomic */
static char *txt_num(const char *prefix, int32_t v)
{
    char *b = txt_next();
    if (prefix && *prefix) {
        strcpy_(b, prefix);
        fmt_int(txt_end(b), v);
    } else {
        fmt_int(b, v);
    }
    return b;
}
