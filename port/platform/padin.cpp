/* padin - live pad input: SDL3 gamepad + host key provider -> PadFrame (see padin.h). */
#include "padin.h"
#include "plat.h"
#include <string.h>
#include "sdl3.h"

/* ---- SDL3: the shared run-time loader + event pump (sdl3.h) */

static int sdl_state;   /* 0 not tried, 1 ready, -1 unavailable */
static PadinConfig cfg;
static int cfg_set;
static padin_key_fn key_fn;
static void *key_user;
/* player slots: 0 = player 1 (with the keyboard), 1 = player 2 (cfg.pad2). A slot keeps its gamepad until that one is
 * removed; an empty slot takes the first listed gamepad no other slot holds (slot 0 first). */
struct Slot {
    SDL_Gamepad *gp;
    SDL_JoystickID id;
    int stick_on[2], trig_on[2];   /* hysteresis state: left / right stick outside the dead zone, L2 / R2 down */
    int read;                      /* the gamepad was read in the last sample (rumble follows it) */
    uint16_t rum_lo, rum_hi;       /* last rumble command sent (0, 0 = at rest; the state of a newly opened pad) */
    int rum_age;                   /* samples since it was sent */
};
static Slot slots[PADIN_PLAYERS];
static int rescan = 1;
static SDL_JoystickID vpad_id[PADIN_PLAYERS];
static SDL_Joystick *vpad[PADIN_PLAYERS];
static int vpad_rum_n[PADIN_PLAYERS];
static uint16_t vpad_rum_lo[PADIN_PLAYERS], vpad_rum_hi[PADIN_PLAYERS];
static unsigned rumble_n[PADIN_PLAYERS];

static void pad_event(const SDL_Event *e);

static int sdl_load(void)
{
    if (sdl_state) return sdl_state > 0;
    sdl_state = -1;
    if (!sdl3_load()) { plat_log("padin: SDL3 unavailable - keyboard only"); return 0; }
    sdl3.SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); /* focus policy is ours (cfg.background) */
    if (!sdl3.SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        plat_log("padin: SDL_InitSubSystem(GAMEPAD) failed: %s - keyboard only", sdl3.SDL_GetError());
        return 0;
    }
    sdl_state = 1;
    rescan = 1;
    sdl3_add_handler(pad_event);
    int v = sdl3.SDL_GetVersion();
    plat_log("padin: SDL %d.%d.%d gamepad input ready", v / 1000000, v / 1000 % 1000, v % 1000);
    return 1;
}

static void rumble_send(int s, uint16_t lo, uint16_t hi)
{
    Slot *t = &slots[s];
    sdl3.SDL_RumbleGamepad(t->gp, lo, hi, lo || hi ? PADIN_RUMBLE_MS : 0);
    t->rum_lo = lo; t->rum_hi = hi; t->rum_age = 0;
    rumble_n[s]++;
}

static void close_slot(int s)
{
    Slot *t = &slots[s];
    if (t->gp && (t->rum_lo || t->rum_hi) && sdl3.SDL_GamepadConnected(t->gp)) rumble_send(s, 0, 0);   /* pad2 off */
    if (t->gp) { sdl3.SDL_CloseGamepad(t->gp); plat_log("padin: player %d gamepad %u closed", s + 1, (unsigned)t->id); }
    memset(t, 0, sizeof *t);
}

static void close_pads(void) { for (int s = 0; s < PADIN_PLAYERS; s++) close_slot(s); }

static void pick_pads(void)
{
    for (int s = 0; s < PADIN_PLAYERS; s++)
        if (slots[s].gp && !sdl3.SDL_GamepadConnected(slots[s].gp)) { close_slot(s); rescan = 1; }
    if (!cfg.pad2 && slots[1].gp) close_slot(1);
    if (!rescan) return;
    rescan = 0;
    int n = 0;
    SDL_JoystickID *ids = sdl3.SDL_GetGamepads(&n);
    for (int s = 0; s < (cfg.pad2 ? 2 : 1); s++) {
        if (slots[s].gp) continue;
        for (int i = s == 0 && cfg.gamepad > 1 ? cfg.gamepad - 1 : 0; ids && i < n && !slots[s].gp; i++) {
            if (cfg.virtual_only && !sdl3.SDL_IsJoystickVirtual(ids[i])) continue;
            int held = 0;
            for (int o = 0; o < PADIN_PLAYERS; o++) held |= slots[o].gp && slots[o].id == ids[i];
            if (held) continue;
            if ((slots[s].gp = sdl3.SDL_OpenGamepad(ids[i]))) {
                slots[s].id = ids[i];
                const char *nm = sdl3.SDL_GetGamepadName(slots[s].gp);
                plat_log("padin: player %d gamepad %u open: %s", s + 1, (unsigned)ids[i], nm ? nm : "?");
            }
        }
    }
    if (ids) sdl3.SDL_free(ids);
}

static void pad_event(const SDL_Event *e)
{
    if (sdl_state <= 0) return;   /* padin_shutdown: the handler stays registered */
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) rescan = 1;
    else if (e->type == SDL_EVENT_GAMEPAD_REMOVED)
        for (int s = 0; s < PADIN_PLAYERS; s++)
            if (slots[s].gp && e->gdevice.which == slots[s].id) { close_slot(s); rescan = 1; }
}

static void pump(void)
{
    sdl3_pump();   /* every handler (video: window events) sees the queue */
    pick_pads();
}

void padin_config_default(PadinConfig *c)
{
    memset(c, 0, sizeof *c);
    c->gamepad = 1;
    c->keyboard = 1;
    c->background = 1;
    c->deadzone = 13;
    c->trigger = 25;
    c->rumble = 100;
    c->pad2 = 1;
    for (int a = 0; a < PADIN_NACT; a++) padin_parse_keys(padin_actions[a].def, c->keys[a]);
}

void padin_configure(const PadinConfig *c, padin_key_fn keys, void *user)
{
    int vo = cfg.virtual_only, gp = cfg.gamepad;
    cfg = *c;
    cfg_set = 1;
    if (cfg.deadzone < 0) cfg.deadzone = 0;
    if (cfg.deadzone > 90) cfg.deadzone = 90;
    if (cfg.trigger < 1) cfg.trigger = 1;
    if (cfg.trigger > 100) cfg.trigger = 100;
    key_fn = keys;
    key_user = user;
    if (sdl_state > 0 && (vo != cfg.virtual_only || gp != cfg.gamepad)) close_pads();   /* player 1 re-picks */
    rescan = 1;   /* virtual_only / pad2 may have changed: re-pick on the next poll */
}

static uint8_t axis_byte(int v)
{
    int x = (v >> 8) + 128;
    return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

/* radial dead zone + hysteresis: on above dz, off below dz - 2 % of full deflection */
static void read_stick(Slot *t, int s, SDL_GamepadAxis ax, SDL_GamepadAxis ay, uint8_t *bx, uint8_t *by)
{
    int x = sdl3.SDL_GetGamepadAxis(t->gp, ax), y = sdl3.SDL_GetGamepadAxis(t->gp, ay);
    long long m2 = (long long)x * x + (long long)y * y;
    long long on = (long long)cfg.deadzone * 32767 / 100, off = on - 655;
    if (off < 0) off = 0;
    if (cfg.deadzone == 0) t->stick_on[s] = 1;
    else if (t->stick_on[s]) t->stick_on[s] = m2 >= off * off;
    else t->stick_on[s] = m2 > on * on;
    if (t->stick_on[s]) { *bx = axis_byte(x); *by = axis_byte(y); }
}

static int read_trigger(Slot *s, int t, SDL_GamepadAxis a)
{
    int v = sdl3.SDL_GetGamepadAxis(s->gp, a);
    int on = cfg.trigger * 32767 / 100, off = on - 655;
    s->trig_on[t] = s->trig_on[t] ? v >= off : v >= on;
    return s->trig_on[t];
}

/* one slot's gamepad -> f (neutral if the slot is empty or not read now) */
static void read_slot(Slot *t, int read, PadFrame *f)
{
    uint16_t b = 0;
    uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    if (t->gp && read) {
        static const struct { SDL_GamepadButton sb; int bit; } bm[] = {
            {SDL_GAMEPAD_BUTTON_SOUTH, 6}, {SDL_GAMEPAD_BUTTON_EAST, 5}, {SDL_GAMEPAD_BUTTON_WEST, 7},
            {SDL_GAMEPAD_BUTTON_NORTH, 4}, {SDL_GAMEPAD_BUTTON_BACK, 8}, {SDL_GAMEPAD_BUTTON_START, 11},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK, 9}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 10},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 2}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 3},
            {SDL_GAMEPAD_BUTTON_DPAD_UP, 12}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 13},
            {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 14}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 15},
        };
        for (unsigned i = 0; i < sizeof bm / sizeof *bm; i++)
            if (sdl3.SDL_GetGamepadButton(t->gp, bm[i].sb)) b |= 1u << bm[i].bit;
        if (read_trigger(t, 0, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) b |= 1u << 0;
        if (read_trigger(t, 1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) b |= 1u << 1;
        read_stick(t, 0, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, &lx, &ly);
        read_stick(t, 1, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, &rx, &ry);
    } else t->stick_on[0] = t->stick_on[1] = t->trig_on[0] = t->trig_on[1] = 0;
    f->buttons = b;
    f->lx = lx; f->ly = ly; f->rx = rx; f->ry = ry;
}

int padin_poll2(int focused, PadFrame out[PADIN_PLAYERS])
{
    if (!cfg_set) padin_config_default(&cfg), cfg_set = 1;
    int sdl_on = cfg.gamepad && sdl_load();
    if (sdl_on) pump();
    for (int s = 0; s < PADIN_PLAYERS; s++) {
        slots[s].read = sdl_on && (focused || cfg.background);
        read_slot(&slots[s], slots[s].read, &out[s]);
    }
    uint16_t b = out[0].buttons;
    uint8_t lx = out[0].lx, ly = out[0].ly, rx = out[0].rx, ry = out[0].ry;
    if (focused && key_fn && cfg.keyboard) {   /* the keyboard is player 1 */
        int down[PADIN_NACT];
        for (int a = 0; a < PADIN_NACT; a++) {
            down[a] = 0;
            for (int k = 0; k < PADIN_KEYS_PER; k++)
                if (cfg.keys[a][k] && key_fn(key_user, cfg.keys[a][k])) down[a] = 1;
        }
        for (int a = 0; a < 16; a++) if (down[a]) b |= 1u << a;
        /* same precedence as the old keyboard code: left / up first, right / down win when both are held */
        if (down[PADIN_A_LSTICK_LEFT]) lx = 0;
        if (down[PADIN_A_LSTICK_RIGHT]) lx = 255;
        if (down[PADIN_A_LSTICK_UP]) ly = 0;
        if (down[PADIN_A_LSTICK_DOWN]) ly = 255;
        if (down[PADIN_A_RSTICK_LEFT]) rx = 0;
        if (down[PADIN_A_RSTICK_RIGHT]) rx = 255;
        if (down[PADIN_A_RSTICK_UP]) ry = 0;
        if (down[PADIN_A_RSTICK_DOWN]) ry = 255;
    }
    out[0].buttons = b;
    out[0].lx = lx; out[0].ly = ly; out[0].rx = rx; out[0].ry = ry;
    return cfg.pad2 && slots[1].gp ? 1 : 0;
}

void padin_poll(int focused, PadFrame *out)
{
    PadFrame f[PADIN_PLAYERS];
    padin_poll2(focused, f);
    *out = f[0];
}

int padin_gamepad(char *name, size_t n) { return padin_gamepad_n(0, name, n); }

int padin_gamepad_n(int player, char *name, size_t n)
{
    if (name && n) name[0] = 0;
    if (player < 0 || player >= PADIN_PLAYERS || !slots[player].gp) return 0;
    if (name && n) {
        const char *nm = sdl3.SDL_GetGamepadName(slots[player].gp);
        size_t i = 0;
        for (; nm && nm[i] && i + 1 < n; i++) name[i] = nm[i];
        name[i] = 0;
    }
    return 1;
}

void padin_rumble(int player, int small, int large)
{
    if (sdl_state <= 0 || player < 0 || player >= PADIN_PLAYERS || !slots[player].gp) return;
    Slot *t = &slots[player];
    int pct = cfg.rumble < 0 ? 0 : cfg.rumble > 100 ? 100 : cfg.rumble;
    uint16_t lo = 0, hi = 0;
    if (pct && t->read) {
        lo = (uint16_t)((unsigned)(large & 255) * 257u * (unsigned)pct / 100u);
        hi = small ? (uint16_t)(0xffffu * (unsigned)pct / 100u) : 0;
    }
    if (lo != t->rum_lo || hi != t->rum_hi) rumble_send(player, lo, hi);
    else if ((lo || hi) && ++t->rum_age >= PADIN_RUMBLE_REFRESH) rumble_send(player, lo, hi);   /* SDL extends expiry */
}

void padin_rumble_off(void)
{
    if (sdl_state <= 0) return;
    for (int s = 0; s < PADIN_PLAYERS; s++) {
        SDL_Gamepad *gp = slots[s].gp;   /* SDL3 validates the pointer, so a pad closed meanwhile only fails */
        if (gp && (slots[s].rum_lo || slots[s].rum_hi)) rumble_send(s, 0, 0);
    }
}

unsigned padin_rumble_calls(int player) { return player >= 0 && player < PADIN_PLAYERS ? rumble_n[player] : 0; }

void padin_shutdown(void)
{
    if (sdl_state <= 0) return;
    padin_rumble_off();
    for (int i = 0; i < PADIN_PLAYERS; i++) padin_vpad_detach_n(i);
    close_pads();
    sdl3.SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    sdl_state = 0;   /* the library stays mapped; the next padin_poll initialises again */
    rescan = 1;
}

/* ---- virtual gamepads (tests) */
static bool SDLCALL vpad_rumble(void *user, Uint16 lo, Uint16 hi)
{
    int i = (int)(intptr_t)user;
    vpad_rum_n[i]++; vpad_rum_lo[i] = lo; vpad_rum_hi[i] = hi;
    return true;
}

int padin_vpad_rumble_n(int i, uint16_t *low, uint16_t *high)
{
    if (i < 0 || i >= PADIN_PLAYERS) return 0;
    if (low) *low = vpad_rum_lo[i];
    if (high) *high = vpad_rum_hi[i];
    return vpad_rum_n[i];
}

int padin_vpad_attach_n(int i)
{
    if (i < 0 || i >= PADIN_PLAYERS) return 0;
    if (vpad[i] || !sdl_load()) return vpad[i] != 0;
    SDL_VirtualJoystickDesc d;
    memset(&d, 0, sizeof d);
    d.version = sizeof d;
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.name = i ? "padin virtual gamepad 2" : "padin virtual gamepad";
    d.Rumble = vpad_rumble;
    d.userdata = (void *)(intptr_t)i;
    vpad_rum_n[i] = 0; vpad_rum_lo[i] = vpad_rum_hi[i] = 0;
    vpad_id[i] = sdl3.SDL_AttachVirtualJoystick(&d);
    if (!vpad_id[i]) { plat_log("padin: virtual joystick: %s", sdl3.SDL_GetError()); return 0; }
    vpad[i] = sdl3.SDL_OpenJoystick(vpad_id[i]);
    if (!vpad[i]) {
        plat_log("padin: open virtual joystick: %s", sdl3.SDL_GetError());
        sdl3.SDL_DetachVirtualJoystick(vpad_id[i]);
        vpad_id[i] = 0;
    }
    return vpad[i] != 0;
}

void padin_vpad_detach_n(int i)
{
    if (i < 0 || i >= PADIN_PLAYERS || !vpad[i]) return;
    sdl3.SDL_CloseJoystick(vpad[i]);
    sdl3.SDL_DetachVirtualJoystick(vpad_id[i]);
    vpad[i] = 0; vpad_id[i] = 0;
}

void padin_vpad_button_n(int i, int b, int down)
{
    if (i >= 0 && i < PADIN_PLAYERS && vpad[i]) sdl3.SDL_SetJoystickVirtualButton(vpad[i], b, down != 0);
}

/* a virtual gamepad's trigger axes are full range (-32768 = released); v is in SDL_GetGamepadAxis terms (0..32767) */
void padin_vpad_axis_n(int i, int a, int v)
{
    if (i < 0 || i >= PADIN_PLAYERS || !vpad[i]) return;
    if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) v = v * 2 - 32768;
    sdl3.SDL_SetJoystickVirtualAxis(vpad[i], a, (Sint16)(v < -32768 ? -32768 : v > 32767 ? 32767 : v));
}

int padin_vpad_attach(void) { return padin_vpad_attach_n(0); }
void padin_vpad_detach(void) { padin_vpad_detach_n(0); }
void padin_vpad_button(int b, int down) { padin_vpad_button_n(0, b, down); }
void padin_vpad_axis(int a, int v) { padin_vpad_axis_n(0, a, v); }
