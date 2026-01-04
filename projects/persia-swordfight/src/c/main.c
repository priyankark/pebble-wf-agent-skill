#include <pebble.h>
#include <stdlib.h>

// Toggle subtle camera shake on sword clashes (0 = off)
#define ENABLE_CLASH_SHAKE 1

// ===========================================================================
// PRINCE OF PERSIA - DETAILED CHARACTERS & SMOOTH ANIMATION
// ===========================================================================

#ifdef PBL_ROUND
    #define SCREEN_W 180
    #define SCREEN_H 180
    #define GROUND_Y 162
    #define PRINCE_X 55
    #define GUARD_X 125
#else
    #define SCREEN_W 144
    #define SCREEN_H 168
    #define GROUND_Y 150
    #define PRINCE_X 38
    #define GUARD_X 106
#endif

#define ANIM_MS 22  // Fast action!

// ===========================================================================
// COLORS
// ===========================================================================
#ifdef PBL_COLOR
    #define COL_SKY1       GColorOrange
    #define COL_SKY2       GColorRajah
    #define COL_SKY3       GColorYellow
    #define COL_GROUND     GColorDarkGray
    #define COL_PRINCE     GColorWhite
    #define COL_PRINCE_V   GColorCyan        // Prince vest
    #define COL_GUARD      GColorBlack
    #define COL_GUARD_V    GColorDarkGray    // Guard vest
    #define COL_SWORD_P    GColorWhite
    #define COL_SWORD_G    GColorLightGray
    #define COL_SKIN       GColorMelon
    #define COL_HAIR       GColorBlack
    #define COL_BELT       GColorRed
    #define COL_TIME       GColorWhite
    #define COL_DATE       GColorLightGray
    #define COL_SPARK      GColorYellow
#else
    #define COL_SKY1       GColorWhite
    #define COL_SKY2       GColorLightGray
    #define COL_SKY3       GColorWhite
    #define COL_GROUND     GColorDarkGray
    #define COL_PRINCE     GColorWhite
    #define COL_PRINCE_V   GColorLightGray
    #define COL_GUARD      GColorBlack
    #define COL_GUARD_V    GColorDarkGray
    #define COL_SWORD_P    GColorBlack
    #define COL_SWORD_G    GColorBlack
    #define COL_SKIN       GColorWhite
    #define COL_HAIR       GColorBlack
    #define COL_BELT       GColorBlack
    #define COL_TIME       GColorBlack
    #define COL_DATE       GColorBlack
    #define COL_SPARK      GColorWhite
#endif

// ===========================================================================
// POSE SYSTEM - Target values that we interpolate toward
// ===========================================================================
typedef struct {
    int16_t lean;       // Body lean
    int16_t step_fwd;   // Front foot forward
    int16_t step_back;  // Back foot back
    int16_t crouch;     // Crouch amount
    int16_t sword_ang;  // Sword angle (degrees)
    int16_t arm_raise;  // Arm height offset
} PoseData;

// All pose definitions - Angles: 0=UP, 90=horizontal, 180=DOWN
// For proper X-clash: attacker swings DOWN (>90), blocker catches UP (<90)
static const PoseData POSES[] = {
    // P_READY - neutral guard position
    {3, 6, 0, 0, 75, 0},
    // P_STEP_FWD - advancing
    {6, 10, 0, 3, 80, -2},
    // P_THRUST - horizontal lunge
    {12, 16, 0, 8, 95, -8},
    // P_SLASH - BIG DOWNWARD SWING (angle > 90 = tip below hand)
    {10, 12, 0, 5, 135, 12},
    // P_BLOCK_HIGH - sword UP to catch downward slash (angle < 90)
    {2, 6, 0, 2, 45, 10},
    // P_BLOCK_LOW - parry low (angle slightly > 90)
    {4, 8, 0, 4, 105, -4},
    // P_STRUCK - reeling back, sword wild
    {-16, -6, 12, 12, 160, 8},
    // P_STEP_BACK - retreating
    {-8, 0, 10, 2, 70, 0},
};

typedef enum {
    P_READY, P_STEP_FWD, P_THRUST, P_SLASH,
    P_BLOCK_H, P_BLOCK_L, P_STRUCK, P_STEP_BACK
} Pose;

typedef struct {
    int16_t x;
    int8_t dir;
    Pose pose;
    // Current interpolated values
    int16_t cur_lean, cur_step_fwd, cur_step_back, cur_crouch;
    int16_t cur_sword_ang, cur_arm_raise;
} Fighter;

typedef struct {
    Pose prince;
    Pose guard;
    int16_t dur;
    bool clash;
} Move;

// ===========================================================================
// CHOREOGRAPHY - Attack vs Block for X-shaped sword clashes!
// ===========================================================================
static const Move s_seq[] = {
    // Opening stance
    {P_READY, P_READY, 16, false},

    // Prince SLASHES down, Guard BLOCKS high - X CLASH!
    {P_SLASH, P_BLOCK_H, 10, true},

    // Guard counters with slash, Prince blocks - X CLASH!
    {P_BLOCK_H, P_SLASH, 10, true},

    // Quick ready
    {P_READY, P_READY, 6, false},

    // Prince thrusts low, Guard parries - CLASH!
    {P_THRUST, P_BLOCK_L, 10, true},

    // Guard thrusts, Prince parries - CLASH!
    {P_BLOCK_L, P_THRUST, 10, true},

    // Flurry! Alternating slashes and blocks
    {P_SLASH, P_BLOCK_H, 8, true},
    {P_BLOCK_H, P_SLASH, 8, true},
    {P_SLASH, P_BLOCK_H, 8, true},

    // Brief pause
    {P_READY, P_READY, 6, false},

    // Prince gets aggressive - rapid attacks!
    {P_THRUST, P_BLOCK_L, 8, true},
    {P_SLASH, P_BLOCK_H, 8, true},
    {P_THRUST, P_BLOCK_L, 8, true},

    // Guard gets HIT!
    {P_SLASH, P_STRUCK, 12, false},
    {P_READY, P_STEP_BACK, 8, false},

    // Guard recovers and counters
    {P_READY, P_READY, 8, false},
    {P_BLOCK_H, P_SLASH, 10, true},
    {P_BLOCK_L, P_THRUST, 8, true},

    // Prince gets HIT!
    {P_STRUCK, P_SLASH, 12, false},
    {P_STEP_BACK, P_READY, 8, false},

    // Final exchange
    {P_READY, P_READY, 8, false},
    {P_SLASH, P_BLOCK_H, 8, true},
    {P_BLOCK_H, P_SLASH, 8, true},
    {P_THRUST, P_BLOCK_L, 8, true},
    {P_BLOCK_L, P_THRUST, 8, true},

    // Reset
    {P_READY, P_READY, 12, false},
};
// Use compile-time array length to prevent mismatches
#define NUM_SEQ ((int)(sizeof(s_seq) / sizeof(s_seq[0])))

// ===========================================================================
// GLOBALS
// ===========================================================================
static Window *s_win;
static Layer *s_canvas;
static TextLayer *s_time_lyr, *s_date_lyr, *s_batt_lyr;
static AppTimer *s_timer;

static Fighter s_prince, s_guard;
static int s_seq_idx = 0, s_seq_frame = 0, s_gframe = 0;
static int s_battery = 100;
static char s_time_buf[8], s_date_buf[16], s_batt_buf[8];

static bool s_sparks = false;
static int s_spark_life = 0;
static int16_t s_spark_x, s_spark_y;

// Subtle camera shake on clashes
static int8_t s_shake_frames = 0;
static int8_t s_shake_mag = 0;
static int8_t s_shake_dx = 0;
static int8_t s_shake_dy = 0;
// Haptics cooldown (frames) to avoid over-vibrating
static int s_vibe_cooldown = 0;

// Forward decls
static void update_fighter_interpolation(Fighter *f);
static void compute_sword_points(const Fighter *f, GPoint *hand, GPoint *tip);
static bool line_intersect(GPoint a1, GPoint a2, GPoint b1, GPoint b2, int16_t *ix, int16_t *iy);

// ===========================================================================
// SMOOTH INTERPOLATION
// ===========================================================================
static int16_t lerp(int16_t current, int16_t target, int16_t speed) {
    int16_t diff = target - current;
    if (diff > speed) return current + speed;
    if (diff < -speed) return current - speed;
    return target;
}

static void update_fighter_interpolation(Fighter *f) {
    PoseData target = POSES[f->pose];
    int16_t spd = 4;  // Faster body movement

    f->cur_lean = lerp(f->cur_lean, target.lean, spd + 1);
    f->cur_step_fwd = lerp(f->cur_step_fwd, target.step_fwd, spd + 2);
    f->cur_step_back = lerp(f->cur_step_back, target.step_back, spd + 2);
    f->cur_crouch = lerp(f->cur_crouch, target.crouch, spd);
    f->cur_sword_ang = lerp(f->cur_sword_ang, target.sword_ang, 14);  // Fast sword!
    f->cur_arm_raise = lerp(f->cur_arm_raise, target.arm_raise, spd + 2);
}

// ===========================================================================
// SWORD POINTS HELPERS (for dynamic spark placement)
// ===========================================================================
static void compute_sword_points(const Fighter *f, GPoint *hand, GPoint *tip) {
    // Mirror the geometry used in draw_fighter to keep effects aligned
    int d = f->dir;

    // Base position (optionally include shake to align with visuals)
    int base_x = f->x;
    int base_y = GROUND_Y;
#if ENABLE_CLASH_SHAKE
    base_x += s_shake_dx;
    base_y += s_shake_dy;
#endif

    int lean = f->cur_lean * d;
    int crouch = f->cur_crouch;
    int sword_ang = f->cur_sword_ang;
    int arm_raise = f->cur_arm_raise;

    int cx = base_x + lean;
    int cy = base_y + crouch;

    int shoulder_y = cy - 52;

    // Sword arm origin
    int sarm_x = cx + 6 * d;
    int sarm_y = shoulder_y + 5 - arm_raise;

    // Angle math
    int32_t ang = (sword_ang * TRIG_MAX_ANGLE) / 360;
    int sin_val = sin_lookup(ang);
    int cos_val = cos_lookup(ang);
    int x_dir = d;

    // Upper arm to elbow
    int elbow_dist = 10;
    int elbow_x = sarm_x + x_dir * (sin_val * elbow_dist) / TRIG_MAX_RATIO;
    int elbow_y = sarm_y - (cos_val * elbow_dist) / TRIG_MAX_RATIO;

    // Forearm to hand
    int arm_len = 10;
    int hand_x = elbow_x + x_dir * (sin_val * arm_len) / TRIG_MAX_RATIO;
    int hand_y = elbow_y - (cos_val * arm_len) / TRIG_MAX_RATIO;

    // Sword tip
    int slen = 50;
    int tip_x = hand_x + x_dir * (sin_val * slen) / TRIG_MAX_RATIO;
    int tip_y = hand_y - (cos_val * slen) / TRIG_MAX_RATIO;

    if (hand) *hand = (GPoint){ hand_x, hand_y };
    if (tip) *tip  = (GPoint){ tip_x, tip_y };
}

static bool line_intersect(GPoint a1, GPoint a2, GPoint b1, GPoint b2, int16_t *ix, int16_t *iy) {
    // Integer line intersection using determinants
    int32_t x1 = a1.x, y1 = a1.y;
    int32_t x2 = a2.x, y2 = a2.y;
    int32_t x3 = b1.x, y3 = b1.y;
    int32_t x4 = b2.x, y4 = b2.y;

    int32_t den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (den == 0) return false;

    int32_t num_t = (x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4);
    int32_t num_u = (x1 - x3) * (y1 - y2) - (y1 - y3) * (x1 - x2);

    // Check segment overlap 0..1 using integer space
    // Instead of dividing, compare signs and ranges approximately
    // We compute t and u with integer division for position
    int32_t t_num = num_t;
    int32_t u_num = num_u;

    // Intersection point
    int32_t ix32 = x1 * den + t_num * (x2 - x1);
    int32_t iy32 = y1 * den + t_num * (y2 - y1);

    // Determine if within segments: 0 <= t,u <= den, considering sign of den
    bool within_t = (den > 0) ? (t_num >= 0 && t_num <= den) : (t_num <= 0 && t_num >= den);
    bool within_u = (den > 0) ? (u_num >= 0 && u_num <= den) : (u_num <= 0 && u_num >= den);

    if (within_t && within_u) {
        if (ix) *ix = (int16_t)(ix32 / den);
        if (iy) *iy = (int16_t)(iy32 / den);
        return true;
    }
    return false;
}

// ===========================================================================
// DRAW DETAILED CHARACTER
// ===========================================================================
static void draw_fighter(GContext *ctx, Fighter *f, bool is_prince) {
    int x = f->x;
    int y = GROUND_Y;
#if ENABLE_CLASH_SHAKE
    // Optional subtle camera shake
    x += s_shake_dx;
    y += s_shake_dy;
#endif
    int d = f->dir;

    // Use interpolated values
    int lean = f->cur_lean * d;
    int step_fwd = f->cur_step_fwd * d;
    int step_back = f->cur_step_back * d;
    int crouch = f->cur_crouch;
    int sword_ang = f->cur_sword_ang;
    int arm_raise = f->cur_arm_raise;

    int cx = x + lean;
    int cy = y + crouch;

    // Colors
    GColor pants_col = is_prince ? COL_PRINCE : COL_GUARD;
    GColor vest_col = is_prince ? COL_PRINCE_V : COL_GUARD_V;
    GColor sword_col = is_prince ? COL_SWORD_P : COL_SWORD_G;

    // For aplite (B/W), give the Prince a black outline to improve silhouette
    bool outline_white = false;
#ifndef PBL_COLOR
    if (is_prince) outline_white = true;
#endif

    // Helper to draw an outlined line for better readability on B/W
    void draw_line_outlined(GPoint a, GPoint b, int width, GColor color, bool outline) {
#ifndef PBL_COLOR
        if (outline) {
            graphics_context_set_stroke_color(ctx, GColorBlack);
            graphics_context_set_stroke_width(ctx, width + 2);
            graphics_draw_line(ctx, a, b);
        }
#endif
        graphics_context_set_stroke_color(ctx, color);
        graphics_context_set_stroke_width(ctx, width);
        graphics_draw_line(ctx, a, b);
    }

    // === LEGS WITH BAGGY PANTS ===
    int hip_y = cy - 30;
    int knee_y = cy - 12;
    int back_foot = x - step_back;
    int front_foot = x + step_fwd;
    int back_knee = x + (back_foot - x) / 2 - 3 * d;
    int front_knee = x + step_fwd / 2 + 5 * d;
    int fk_y = knee_y - (f->pose == P_THRUST ? 6 : 0);

    // Back leg - thigh (baggy)
    draw_line_outlined(GPoint(cx - 3*d, hip_y), GPoint(back_knee, knee_y), 11, pants_col, outline_white);
    // Back leg - calf (tapered)
    draw_line_outlined(GPoint(back_knee, knee_y), GPoint(back_foot, cy - 2), 5, pants_col, outline_white);

    // Front leg - thigh (baggier)
    draw_line_outlined(GPoint(cx + 3*d, hip_y), GPoint(front_knee, fk_y), 13, pants_col, outline_white);
    // Front leg - calf
    draw_line_outlined(GPoint(front_knee, fk_y), GPoint(front_foot, cy - 2), 5, pants_col, outline_white);

    // Ankle wraps / gathered pants
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, COL_HAIR);
    graphics_draw_line(ctx, GPoint(back_foot - 3, cy - 4), GPoint(back_foot + 3, cy - 4));
    graphics_draw_line(ctx, GPoint(front_foot - 3, cy - 4), GPoint(front_foot + 3, cy - 4));

    // Feet (pointed shoes)
    graphics_context_set_fill_color(ctx, COL_HAIR);
    graphics_fill_rect(ctx, GRect(back_foot - 2, cy - 3, 7, 4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(front_foot - 2, cy - 3, 7, 4), 0, GCornerNone);

    // === TORSO ===
    int shoulder_y = cy - 52;
    int chest_y = cy - 42;
    int waist_y = cy - 32;

    // Torso base (vest)
    draw_line_outlined(GPoint(cx, shoulder_y + 2), GPoint(cx, chest_y), 10, vest_col, outline_white);

    // Waist (slimmer with belt)
    draw_line_outlined(GPoint(cx, chest_y), GPoint(cx, waist_y), 6, vest_col, outline_white);

    // Belt / sash
    graphics_context_set_stroke_color(ctx, COL_BELT);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(cx - 5, waist_y - 1), GPoint(cx + 5, waist_y - 1));

    // Sash tail hanging
    #ifdef PBL_COLOR
    if (is_prince) {
        graphics_draw_line(ctx, GPoint(cx + 4*d, waist_y), GPoint(cx + 6*d, waist_y + 8));
    }
    #endif

    // === BACK ARM ===
    int back_arm_x = cx - 6 * d;
    graphics_context_set_stroke_color(ctx, COL_SKIN);
    graphics_context_set_stroke_width(ctx, 4);

    if (f->pose == P_THRUST) {
        // Arm stretched back for balance
        draw_line_outlined(GPoint(back_arm_x, shoulder_y + 5),
                           GPoint(back_arm_x - 12*d, shoulder_y + 14), 4, COL_SKIN, outline_white);
        // Hand
        graphics_context_set_fill_color(ctx, COL_SKIN);
        graphics_fill_circle(ctx, GPoint(back_arm_x - 13*d, shoulder_y + 15), 3);
    } else {
        // Arm at side or slightly bent
        int elbow_x = back_arm_x - 4*d;
        int elbow_y = shoulder_y + 14;
        draw_line_outlined(GPoint(back_arm_x, shoulder_y + 5), GPoint(elbow_x, elbow_y), 4, COL_SKIN, outline_white);
        draw_line_outlined(GPoint(elbow_x, elbow_y), GPoint(elbow_x - 2*d, waist_y - 2), 4, COL_SKIN, outline_white);
    }

    // === HEAD ===
    int head_x = cx;
    int head_y = cy - 62;

    // Hair (back layer)
    graphics_context_set_fill_color(ctx, COL_HAIR);
    graphics_fill_circle(ctx, GPoint(head_x - 3*d, head_y - 2), 6);
    graphics_fill_circle(ctx, GPoint(head_x - 6*d, head_y + 1), 4);

    // Face
    graphics_context_set_fill_color(ctx, COL_SKIN);
    graphics_fill_circle(ctx, GPoint(head_x, head_y), 7);

    // Hair (top)
    graphics_context_set_fill_color(ctx, COL_HAIR);
    graphics_fill_circle(ctx, GPoint(head_x, head_y - 5), 5);

    // Headband
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, is_prince ? COL_BELT : COL_GUARD_V);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(head_x - 6, head_y - 2), GPoint(head_x + 6, head_y - 2));
    // Headband tail
    if (is_prince) {
        graphics_draw_line(ctx, GPoint(head_x - 6, head_y - 2), GPoint(head_x - 10, head_y + 4));
    }
    #endif

    // Eye (simple dot)
    graphics_context_set_fill_color(ctx, COL_HAIR);
    graphics_fill_circle(ctx, GPoint(head_x + 2*d, head_y - 1), 1);

    // Neck
    draw_line_outlined(GPoint(head_x, head_y + 6), GPoint(cx, shoulder_y + 2), 3, COL_SKIN, outline_white);

    // === SWORD ARM ===
    int sarm_x = cx + 6 * d;
    int sarm_y = shoulder_y + 5 - arm_raise;

    // Convert angle - 90 degrees = pointing toward opponent
    int32_t ang = (sword_ang * TRIG_MAX_ANGLE) / 360;

    // Calculate sword direction - MUST point toward center/opponent
    int sin_val = sin_lookup(ang);
    int cos_val = cos_lookup(ang);

    // Flip X direction for guard so sword points LEFT (toward prince)
    int x_dir = d;  // +1 for prince (right), -1 for guard (left)

    // Upper arm
    int elbow_dist = 10;
    int elbow_x = sarm_x + x_dir * (sin_val * elbow_dist) / TRIG_MAX_RATIO;
    int elbow_y = sarm_y - (cos_val * elbow_dist) / TRIG_MAX_RATIO;

    draw_line_outlined(GPoint(sarm_x, sarm_y), GPoint(elbow_x, elbow_y), 4, COL_SKIN, outline_white);

    // Forearm
    int arm_len = 10;
    int hand_x = elbow_x + x_dir * (sin_val * arm_len) / TRIG_MAX_RATIO;
    int hand_y = elbow_y - (cos_val * arm_len) / TRIG_MAX_RATIO;
    draw_line_outlined(GPoint(elbow_x, elbow_y), GPoint(hand_x, hand_y), 3, COL_SKIN, outline_white);

    // Hand
    graphics_context_set_fill_color(ctx, COL_SKIN);
    graphics_fill_circle(ctx, GPoint(hand_x, hand_y), 3);

    // === SWORD === Long enough to REACH opponent!
    int slen = 50;
    int tip_x = hand_x + x_dir * (sin_val * slen) / TRIG_MAX_RATIO;
    int tip_y = hand_y - (cos_val * slen) / TRIG_MAX_RATIO;

    // Blade - thick and visible
    graphics_context_set_stroke_color(ctx, sword_col);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(hand_x, hand_y), GPoint(tip_x, tip_y));

    // Blade edge highlight
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    int mid_x = hand_x + x_dir * (sin_val * slen / 2) / TRIG_MAX_RATIO;
    int mid_y = hand_y - (cos_val * slen / 2) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(mid_x, mid_y), GPoint(tip_x, tip_y));
    #endif

    // Crossguard
    graphics_context_set_stroke_color(ctx, COL_HAIR);
    graphics_context_set_stroke_width(ctx, 3);
    int hx1 = hand_x - (cos_val * 6) / TRIG_MAX_RATIO;
    int hy1 = hand_y - x_dir * (sin_val * 6) / TRIG_MAX_RATIO;
    int hx2 = hand_x + (cos_val * 6) / TRIG_MAX_RATIO;
    int hy2 = hand_y + x_dir * (sin_val * 6) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(hx1, hy1), GPoint(hx2, hy2));

    // Pommel
    graphics_context_set_fill_color(ctx, COL_HAIR);
    int pom_x = hand_x - x_dir * (sin_val * 4) / TRIG_MAX_RATIO;
    int pom_y = hand_y + (cos_val * 4) / TRIG_MAX_RATIO;
    graphics_fill_circle(ctx, GPoint(pom_x, pom_y), 2);
}

// ===========================================================================
// BACKGROUND
// ===========================================================================
static void draw_bg(GContext *ctx) {
    int h = 22;
    graphics_context_set_fill_color(ctx, COL_SKY1);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, h), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, COL_SKY2);
    graphics_fill_rect(ctx, GRect(0, h, SCREEN_W, h), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, COL_SKY3);
    graphics_fill_rect(ctx, GRect(0, h*2, SCREEN_W, GROUND_Y - h*2), 0, GCornerNone);

    graphics_context_set_fill_color(ctx, COL_GROUND);
    graphics_fill_rect(ctx, GRect(0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y), 0, GCornerNone);

    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(0, GROUND_Y), GPoint(SCREEN_W, GROUND_Y));
}

// ===========================================================================
// SPARKS - Big flashy sword clangs!
// ===========================================================================
static void draw_sparks(GContext *ctx) {
    if (!s_sparks) return;

    // Outer sparks - yellow
    graphics_context_set_fill_color(ctx, COL_SPARK);
    for (int i = 0; i < 16; i++) {
        int32_t a = (s_gframe * 8000 + i * TRIG_MAX_ANGLE / 16) % TRIG_MAX_ANGLE;
        int dist = 4 + s_spark_life * 3;
        int sx = s_spark_x + (sin_lookup(a) * dist) / TRIG_MAX_RATIO;
        int sy = s_spark_y + (cos_lookup(a) * dist) / TRIG_MAX_RATIO;
#ifndef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_circle(ctx, GPoint(sx, sy), 4);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, GPoint(sx, sy), 3);
#else
        graphics_fill_circle(ctx, GPoint(sx, sy), 3);
#endif
    }

    // Inner sparks
    for (int i = 0; i < 8; i++) {
        int32_t a = (s_gframe * 12000 + i * TRIG_MAX_ANGLE / 8) % TRIG_MAX_ANGLE;
        int dist = 2 + s_spark_life;
        int sx = s_spark_x + (sin_lookup(a) * dist) / TRIG_MAX_RATIO;
        int sy = s_spark_y + (cos_lookup(a) * dist) / TRIG_MAX_RATIO;
#ifndef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_circle(ctx, GPoint(sx, sy), 3);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, GPoint(sx, sy), 2);
#else
        graphics_fill_circle(ctx, GPoint(sx, sy), 2);
#endif
    }

    // Central flash - bright white
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(s_spark_x, s_spark_y), 6);
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(s_spark_x, s_spark_y), 4);
    #else
    // B/W: add black ring for contrast
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(s_spark_x, s_spark_y), 6);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(s_spark_x, s_spark_y), 5);
    #endif
}

// ===========================================================================
// CANVAS
// ===========================================================================
static void canvas_proc(Layer *l, GContext *ctx) {
    draw_bg(ctx);
    draw_fighter(ctx, &s_guard, false);
    draw_fighter(ctx, &s_prince, true);
    draw_sparks(ctx);
}

// ===========================================================================
// ANIMATION
// ===========================================================================
static void init_fighter(Fighter *f, int16_t x, int8_t dir) {
    f->x = x;
    f->dir = dir;
    f->pose = P_READY;
    PoseData p = POSES[P_READY];
    f->cur_lean = p.lean;
    f->cur_step_fwd = p.step_fwd;
    f->cur_step_back = p.step_back;
    f->cur_crouch = p.crouch;
    f->cur_sword_ang = p.sword_ang;
    f->cur_arm_raise = p.arm_raise;
}

static void update_anim(void) {
    s_seq_frame++;
    Move m = s_seq[s_seq_idx];

    if (s_seq_frame >= m.dur) {
        s_seq_frame = 0;
        s_seq_idx = (s_seq_idx + 1) % NUM_SEQ;

        // Do not hard-reset positions at loop; keep continuous flow

        // Keep fighters separate - no overlapping!
        if (s_prince.x < 30) s_prince.x = 30;
        if (s_prince.x > SCREEN_W/2 - 20) s_prince.x = SCREEN_W/2 - 20;
        if (s_guard.x > SCREEN_W - 30) s_guard.x = SCREEN_W - 30;
        if (s_guard.x < SCREEN_W/2 + 20) s_guard.x = SCREEN_W/2 + 20;

        Move next = s_seq[s_seq_idx];
        s_prince.pose = next.prince;
        s_guard.pose = next.guard;

        if (next.clash) {
            // Trigger sparks on impact
            s_sparks = true;
            s_spark_life = 10;

            // Compute where blades cross for spark position
            GPoint p_hand, p_tip, g_hand, g_tip;
            compute_sword_points(&s_prince, &p_hand, &p_tip);
            compute_sword_points(&s_guard, &g_hand, &g_tip);

            int16_t ix = 0, iy = 0;
            if (line_intersect(p_hand, p_tip, g_hand, g_tip, &ix, &iy)) {
                s_spark_x = ix;
                s_spark_y = iy;
            } else {
                // Fallback: average mid-blade points
                GPoint p_mid = (GPoint){ (p_hand.x + p_tip.x) / 2, (p_hand.y + p_tip.y) / 2 };
                GPoint g_mid = (GPoint){ (g_hand.x + g_tip.x) / 2, (g_hand.y + g_tip.y) / 2 };
                s_spark_x = (p_mid.x + g_mid.x) / 2;
                s_spark_y = (p_mid.y + g_mid.y) / 2;
            }

#if ENABLE_CLASH_SHAKE
            // Optional subtle camera shake (battery-friendly)
            if (s_battery > 20) {
                s_shake_frames = 1;  // 1 frame only
                s_shake_mag = 1;     // minimal offset
            }
#endif

            // Light haptic with cooldown to avoid overuse
            if (s_battery > 50 && s_vibe_cooldown == 0) {
                vibes_short_pulse();
                s_vibe_cooldown = 90; // ~2 seconds at 22ms/frame
            }
        }
    }

    // Smooth interpolation every frame
    update_fighter_interpolation(&s_prince);
    update_fighter_interpolation(&s_guard);

    // Movement - keep them on screen and close!
    if (s_prince.pose == P_STEP_FWD && s_prince.x < s_guard.x - 20) s_prince.x += 1;
    else if (s_prince.pose == P_STEP_BACK && s_prince.x > 35) s_prince.x -= 1;
    else if (s_prince.pose == P_STRUCK && s_prince.x > 35) s_prince.x -= 1;

    if (s_guard.pose == P_STEP_FWD && s_guard.x > s_prince.x + 20) s_guard.x -= 1;
    else if (s_guard.pose == P_STEP_BACK && s_guard.x < SCREEN_W - 35) s_guard.x += 1;
    else if (s_guard.pose == P_STRUCK && s_guard.x < SCREEN_W - 35) s_guard.x += 1;

    // Hard bounds - keep separate, swords meet in middle!
    if (s_prince.x < 30) s_prince.x = 30;
    if (s_prince.x > SCREEN_W/2 - 20) s_prince.x = SCREEN_W/2 - 20;
    if (s_guard.x > SCREEN_W - 30) s_guard.x = SCREEN_W - 30;
    if (s_guard.x < SCREEN_W/2 + 20) s_guard.x = SCREEN_W/2 + 20;

    if (s_sparks) {
        s_spark_life--;
        if (s_spark_life <= 0) s_sparks = false;
    }

    // Haptics cooldown decrement
    if (s_vibe_cooldown > 0) {
        s_vibe_cooldown--;
    }

    // Update camera shake offsets (only if enabled)
#if ENABLE_CLASH_SHAKE
    if (s_shake_frames > 0 && s_shake_mag > 0) {
        int m = s_shake_mag;
        s_shake_dx = (s_gframe & 1) ? -m : m;
        s_shake_dy = (s_gframe & 2) ? m : -m;
        s_shake_frames--;
    } else {
        s_shake_dx = 0;
        s_shake_dy = 0;
    }
#else
    s_shake_dx = 0;
    s_shake_dy = 0;
#endif
}

static void timer_cb(void *data) {
    s_gframe++;
    update_anim();
    layer_mark_dirty(s_canvas);

    int ms = (s_battery <= 20) ? ANIM_MS * 2 : ANIM_MS;
    s_timer = app_timer_register(ms, timer_cb, NULL);
}

static void tick_cb(struct tm *t, TimeUnits u) {
    strftime(s_time_buf, sizeof(s_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
    text_layer_set_text(s_time_lyr, s_time_buf);
    strftime(s_date_buf, sizeof(s_date_buf), "%a %b %d", t);
    text_layer_set_text(s_date_lyr, s_date_buf);
}

static void battery_cb(BatteryChargeState s) {
    s_battery = s.charge_percent;
    snprintf(s_batt_buf, sizeof(s_batt_buf), "%d%%", s_battery);
    text_layer_set_text(s_batt_lyr, s_batt_buf);
}

// ===========================================================================
// WINDOW
// ===========================================================================
static void win_load(Window *w) {
    Layer *root = window_get_root_layer(w);
    GRect b = layer_get_bounds(root);

    s_canvas = layer_create(b);
    layer_set_update_proc(s_canvas, canvas_proc);
    layer_add_child(root, s_canvas);

    s_time_lyr = text_layer_create(GRect(0, 4, b.size.w, 32));
    text_layer_set_background_color(s_time_lyr, GColorClear);
    text_layer_set_text_color(s_time_lyr, COL_TIME);
    text_layer_set_font(s_time_lyr, fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS));
    text_layer_set_text_alignment(s_time_lyr, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_lyr));

    // Battery indicator at top right
    s_batt_lyr = text_layer_create(GRect(b.size.w - 38, 4, 36, 16));
    text_layer_set_background_color(s_batt_lyr, GColorClear);
    text_layer_set_text_color(s_batt_lyr, COL_TIME);
    text_layer_set_font(s_batt_lyr, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_batt_lyr, GTextAlignmentRight);
    layer_add_child(root, text_layer_get_layer(s_batt_lyr));

    s_date_lyr = text_layer_create(GRect(0, GROUND_Y + 1, b.size.w, 18));
    text_layer_set_background_color(s_date_lyr, GColorClear);
    text_layer_set_text_color(s_date_lyr, COL_DATE);
    text_layer_set_font(s_date_lyr, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_date_lyr, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_date_lyr));

    init_fighter(&s_prince, PRINCE_X, 1);
    init_fighter(&s_guard, GUARD_X, -1);

    s_timer = app_timer_register(ANIM_MS, timer_cb, NULL);

    time_t now = time(NULL);
    tick_cb(localtime(&now), MINUTE_UNIT);

    // Initialize battery display
    snprintf(s_batt_buf, sizeof(s_batt_buf), "%d%%", s_battery);
    text_layer_set_text(s_batt_lyr, s_batt_buf);
}

static void win_unload(Window *w) {
    if (s_timer) app_timer_cancel(s_timer);
    text_layer_destroy(s_time_lyr);
    text_layer_destroy(s_date_lyr);
    text_layer_destroy(s_batt_lyr);
    layer_destroy(s_canvas);
}

// ===========================================================================
// MAIN
// ===========================================================================
static void init(void) {
    s_win = window_create();
    window_set_background_color(s_win, GColorBlack);
    window_set_window_handlers(s_win, (WindowHandlers){
        .load = win_load, .unload = win_unload
    });
    window_stack_push(s_win, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_cb);
    battery_state_service_subscribe(battery_cb);
    s_battery = battery_state_service_peek().charge_percent;
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    window_destroy(s_win);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
