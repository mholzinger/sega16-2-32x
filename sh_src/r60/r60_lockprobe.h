/* Optional read-only live scroll-lock footer. Game addresses belong to the
 * Altered Beast adapter; this code only reads them and writes the framebuffer.
 * All helpers stay in cart text so the normal hot-code budget is unchanged. */
#ifndef R60_LOCKPROBE_RAM
#define R60_LOCKPROBE_RAM ((const volatile uint8_t *)0x2602CC00u)
#endif
static const uint16_t r60_lp_font[36] = { 0x7b6fu, 0x2c97u, 0x73e7u, 0x73cfu, 0x5bc9u, 0x79cfu, 0x79efu, 0x7292u, 0x7befu, 0x7bcfu, 0x2bedu, 0x6baeu, 0x7927u, 0x6b6eu, 0x79a7u, 0x79a4u, 0x796fu, 0x5bedu, 0x7497u, 0x126fu, 0x5badu, 0x4927u, 0x5fedu, 0x5ffdu, 0x7b6fu, 0x7be4u, 0x7b79u, 0x6badu, 0x79cfu, 0x7492u, 0x5b6fu, 0x5b6au, 0x5bfdu, 0x5aadu, 0x5a92u, 0x72a7u };
static __attribute__((noinline)) void r60_lp_char(unsigned x, unsigned y, unsigned c)
{
    unsigned i = c <= '9' ? c - '0' : c - 'A' + 10u, r, k;
    uint16_t bits = r60_lp_font[i];
    for (r = 0; r < 5u; r++) for (k = 0; k < 3u; k++)
        if ((bits >> ((4u - r) * 3u + 2u - k)) & 1u) {
            volatile uint8_t *q = FB_DIRB((y + r * 2u) * FB_W + x + k * 2u);
            q[0] = q[1] = q[FB_W] = q[FB_W + 1u] = R60_BAR_PEN;
        }
}
static __attribute__((noinline)) void r60_lp_field(unsigned x, unsigned y, unsigned label, uint32_t value, unsigned digits)
{
    unsigned n;
    r60_lp_char(x, y, label);
    for (n = 0; n < digits; n++) {
        unsigned d = (value >> ((digits - 1u - n) * 4u)) & 15u;
        r60_lp_char(x + (n + 1u) * 8u, y, d < 10u ? '0' + d : 'A' + d - 10u);
    }
}
static __attribute__((noinline)) void r60_lockprobe_draw(void)
{
    if (R60_DISPHOLD != 0u || R60_HOLDFLIP) return;
    const volatile uint8_t *b = R60_LOCKPROBE_RAM, *g = b + 0x3000u, *p;
    uint32_t mask = 0, handler = 0, flags = 0, x = 0, y = 0, state = 0;
    unsigned s, count = 0, unseen = 0, wanted, chosen = 40u, r, col;
#define LP16(q) (((uint32_t)(q)[0] << 8) | (q)[1])
    for (s = 16u; s < 40u; s++) if (b[s * 128u] & 128u) {
        mask |= 1u << (s - 16u); count++;
        if (!(b[s * 128u] & 8u)) unseen++;
    }
    wanted = count ? (R60_DIAG[0] / 120u) % (unseen ? unseen : count) : 0;
    for (s = 16u; s < 40u; s++) if ((b[s * 128u] & 128u) && (!unseen || !(b[s * 128u] & 8u))) {
        if (!wanted--) { chosen = s; break; }
    }
    {   /* With no ordinary actors, show Neff/boss record40 instead. */
        p = b + chosen * 128u;
        handler = ((uint32_t)p[2] << 24) | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
        flags = LP16(p); x = LP16(p + 12u); y = LP16(p + 16u); state = p[35];
    }
    for (r = 196u; r < 224u; r++) {
        volatile uint32_t *q = FB_DIR(r * FB_W);
        for (col = 0; col < FB_W / 4u; col++) q[col] = 0;
    }
    r60_lp_field(4,197,'C',LP16(g + 0x132),4);
    r60_lp_field(48,197,'L',LP16(g + 0x13c),4);
    r60_lp_field(92,197,'E',g[0x154],2);
    r60_lp_field(120,197,'W',g[0x15a],2);
    r60_lp_field(148,197,'K',g[0x152],2);
    r60_lp_field(176,197,'V',g[0x143],2);
    r60_lp_field(204,197,'P',b[0x6b],2);
    r60_lp_field(232,197,'D',g[0x1b],2);
    r60_lp_field(4,211,'M',mask,6);
    r60_lp_field(64,211,'S',chosen,2);
    r60_lp_field(92,211,'H',handler,5);
    r60_lp_field(144,211,'X',x,4);
    r60_lp_field(188,211,'Y',y,4);
    r60_lp_field(232,211,'R',flags,4);
    r60_lp_field(276,211,'T',state,2);
    {   /* row 3: the F154 write recorder (sh_src/port_l2.c port_f154_note): U = caught; W = the write
         * site (cport_prep's F154LOG ids, 99 = hand FUN_0000f1c0); O/V = old/new; I = tick; S/G = the
         * record A6 pointed at (S FF = not a record) and its handler; R = first zero-test that read a
         * negative count; F = F152 then. Cache-through read. */
        extern volatile uint8_t port_uf_latch[16];
        const volatile uint8_t *u = (const volatile uint8_t *)(((uint32_t)port_uf_latch) | 0x20000000u);
        r60_lp_field(4,183,'U',u[0],1);
        r60_lp_field(24,183,'W',u[3],2);
        r60_lp_field(52,183,'O',u[4],2);
        r60_lp_field(80,183,'V',u[5],2);
        r60_lp_field(108,183,'I',((uint32_t)u[1] << 8) | u[2],4);
        r60_lp_field(152,183,'S',u[6],2);
        r60_lp_field(180,183,'G',((uint32_t)u[7] << 16) | ((uint32_t)u[8] << 8) | u[9],5);
        r60_lp_field(232,183,'R',u[10],2);
        r60_lp_field(260,183,'F',u[13],2);
    }
#undef LP16
}
