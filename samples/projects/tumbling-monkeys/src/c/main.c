#include <pebble.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define ANIMATION_INTERVAL 50
#define ANIMATION_INTERVAL_LOW_POWER 100
#define LOW_BATTERY_THRESHOLD 20

#define NUM_MONKEYS 3
#define NUM_VINES 6
#define NUM_BRANCHES 3

// Screen dimensions
#ifdef PBL_ROUND
    #define SCREEN_WIDTH 180
    #define SCREEN_HEIGHT 180
    #define CANOPY_TOP 72
    #define GROUND_Y 150
    #define TIME_Y 8
    #define DATE_Y 44
    #define SWING_ZONE_TOP 75
    #define SWING_ZONE_BOTTOM 140
#else
    #define SCREEN_WIDTH 144
    #define SCREEN_HEIGHT 168
    #define CANOPY_TOP 68
    #define GROUND_Y 150
    #define TIME_Y 2
    #define DATE_Y 38
    #define SWING_ZONE_TOP 70
    #define SWING_ZONE_BOTTOM 140
#endif

// ============================================================================
// COLOR PALETTE
// ============================================================================

#ifdef PBL_COLOR
    #define COLOR_SKY GColorPictonBlue
    #define COLOR_SKY_LOW GColorCeleste
    #define COLOR_CANOPY_DARK GColorDarkGreen
    #define COLOR_CANOPY_LIGHT GColorGreen
    #define COLOR_CANOPY_HIGHLIGHT GColorMayGreen
    #define COLOR_VINE GColorArmyGreen
    #define COLOR_BRANCH GColorWindsorTan
    #define COLOR_BRANCH_DARK GColorBulgarianRose
    #define COLOR_GROUND GColorIslamicGreen
    #define COLOR_GROUND_DARK GColorDarkGreen
    #define COLOR_MONKEY_FUR GColorWindsorTan
    #define COLOR_MONKEY_BELLY GColorMelon
    #define COLOR_MONKEY_FACE GColorMelon
    #define COLOR_MONKEY_DARK GColorBlack
    #define COLOR_TIME_TEXT GColorWhite
    #define COLOR_APPLE GColorRed
    #define COLOR_APPLE_BITE GColorPastelYellow
#else
    #define COLOR_SKY GColorWhite
    #define COLOR_SKY_LOW GColorLightGray
    #define COLOR_CANOPY_DARK GColorBlack
    #define COLOR_CANOPY_LIGHT GColorDarkGray
    #define COLOR_CANOPY_HIGHLIGHT GColorLightGray
    #define COLOR_VINE GColorDarkGray
    #define COLOR_BRANCH GColorDarkGray
    #define COLOR_BRANCH_DARK GColorBlack
    #define COLOR_GROUND GColorDarkGray
    #define COLOR_GROUND_DARK GColorBlack
    #define COLOR_MONKEY_FUR GColorWhite
    #define COLOR_MONKEY_BELLY GColorLightGray
    #define COLOR_MONKEY_FACE GColorLightGray
    #define COLOR_MONKEY_DARK GColorBlack
    #define COLOR_TIME_TEXT GColorBlack
    #define COLOR_APPLE GColorDarkGray
    #define COLOR_APPLE_BITE GColorWhite
#endif

// ============================================================================
// TRICK TYPES - COOLER MOVES!
// ============================================================================

typedef enum {
    TRICK_VINE_SWING,       // Swing on vine, release, catch next
    TRICK_CLIMB_VINE,       // Climb up or down a vine
    TRICK_HANG_LOOK,        // Hang and look around
    TRICK_TAIL_HANG,        // Hang by tail, swing
    TRICK_SIT_MUNCH,        // Sit on branch and munch apple
    TRICK_FIGHT,            // Fight with other monkey!
    TRICK_FALLING,          // Falling from tree (shake triggered!)
    TRICK_COUNT
} TrickType;

// Frame counts (at 20 FPS)
#define VINE_SWING_FRAMES 50    // 2.5 sec - swing, release, catch
#define CLIMB_FRAMES 40         // 2 sec - climb up/down
#define HANG_LOOK_FRAMES 60     // 3 sec - chill and look around
#define TAIL_HANG_FRAMES 50     // 2.5 sec - hang by tail
#define SIT_MUNCH_FRAMES 80     // 4 sec - sit and eat
#define FIGHT_FRAMES 60         // 3 sec - monkey fight!
#define FALLING_FRAMES 50       // 2.5 sec - fall, land, recover

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    TrickType current_trick;
    int16_t frame;
    int16_t max_frames;
    GPoint start_pos;
    GPoint end_pos;
    int32_t rotation;
    int vine_index;
    int branch_index;
    int target_branch;
} AnimState;

typedef struct {
    GPoint pos;
    int direction;          // 1 = right, -1 = left
    AnimState anim;
    int32_t tail_phase;
    int32_t limb_phase;     // For arm/leg animation
    bool active;
} Monkey;

typedef struct {
    GPoint top;             // Attachment point
    int16_t length;
    int32_t sway_phase;
    int16_t sway_amount;    // How far it sways
} Vine;

typedef struct {
    GPoint start;
    GPoint end;
    int16_t thickness;
} Branch;

// ============================================================================
// STATIC VARIABLES
// ============================================================================

static Window *s_main_window;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static AppTimer *s_animation_timer;

static Monkey s_monkeys[NUM_MONKEYS];
static Vine s_vines[NUM_VINES];
static Branch s_branches[NUM_BRANCHES];

static int s_battery_level = 100;
static char s_time_buffer[8];
static char s_date_buffer[16];  // "Mon Jan 18"

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static int random_in_range(int min, int max) {
    return min + (rand() % (max - min + 1));
}

static int16_t lerp(int16_t a, int16_t b, int progress) {
    // progress is 0-100
    return a + ((b - a) * progress) / 100;
}

// Smooth ease in-out (slow start, fast middle, slow end)
static int ease_in_out(int progress) {
    // Using sine-based easing for natural motion
    int32_t angle = (progress * TRIG_MAX_ANGLE / 2) / 100;
    int result = 50 - (cos_lookup(angle) * 50) / TRIG_MAX_RATIO;
    return result;
}

// Ease out (fast start, slow end) - for landing
static int ease_out(int progress) {
    return 100 - ((100 - progress) * (100 - progress)) / 100;
}

// Ease in (slow start, fast end) - for takeoff
static int ease_in(int progress) {
    return (progress * progress) / 100;
}

// Bounce ease for playful landing
static int ease_bounce(int progress) {
    if (progress < 70) {
        return ease_in_out(progress * 100 / 70);
    } else {
        // Small bounce at end
        int bounce_progress = (progress - 70) * 100 / 30;
        int bounce = (sin_lookup(bounce_progress * TRIG_MAX_ANGLE / 100) * 15) / TRIG_MAX_RATIO;
        return 100 - bounce;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

static void init_vines(void) {
    // More vines spread across screen for Tarzan swinging
    for (int i = 0; i < NUM_VINES; i++) {
        // Evenly space vines across screen
        s_vines[i].top.x = 15 + (i * (SCREEN_WIDTH - 30) / (NUM_VINES - 1));
        s_vines[i].top.y = CANOPY_TOP - 5;
        s_vines[i].length = random_in_range(35, 50);
        s_vines[i].sway_phase = random_in_range(0, TRIG_MAX_ANGLE - 1);
        s_vines[i].sway_amount = random_in_range(5, 10);
    }
}

static void init_branches(void) {
    // Horizontal branches for climbing and hanging - positioned within canopy
    s_branches[0] = (Branch){
        .start = {10, CANOPY_TOP + 12},
        .end = {SCREEN_WIDTH / 3, CANOPY_TOP + 16},
        .thickness = 4
    };
    s_branches[1] = (Branch){
        .start = {SCREEN_WIDTH / 3 + 10, CANOPY_TOP + 8},
        .end = {2 * SCREEN_WIDTH / 3, CANOPY_TOP + 12},
        .thickness = 5
    };
    s_branches[2] = (Branch){
        .start = {2 * SCREEN_WIDTH / 3 + 5, CANOPY_TOP + 14},
        .end = {SCREEN_WIDTH - 10, CANOPY_TOP + 10},
        .thickness = 4
    };
}

static void select_next_trick(Monkey *m);

static void init_monkeys(void) {
    for (int i = 0; i < NUM_MONKEYS; i++) {
        s_monkeys[i].direction = (i == 0) ? 1 : -1;
        s_monkeys[i].active = true;

        // Spread 3 monkeys across screen on different vines
        if (i == 0) {
            s_monkeys[i].pos.x = SCREEN_WIDTH / 4;
            s_monkeys[i].pos.y = SWING_ZONE_TOP + 15;
            s_monkeys[i].anim.vine_index = 1;
        } else if (i == 1) {
            s_monkeys[i].pos.x = SCREEN_WIDTH / 2;
            s_monkeys[i].pos.y = SWING_ZONE_TOP + 25;
            s_monkeys[i].anim.vine_index = 3;
        } else {
            s_monkeys[i].pos.x = 3 * SCREEN_WIDTH / 4;
            s_monkeys[i].pos.y = SWING_ZONE_TOP + 20;
            s_monkeys[i].anim.vine_index = 5;
        }

        s_monkeys[i].anim.current_trick = TRICK_VINE_SWING;
        s_monkeys[i].anim.frame = random_in_range(0, 20);
        s_monkeys[i].anim.max_frames = VINE_SWING_FRAMES;
        s_monkeys[i].anim.rotation = 0;
        s_monkeys[i].anim.branch_index = -1;

        s_monkeys[i].tail_phase = random_in_range(0, TRIG_MAX_ANGLE - 1);
        s_monkeys[i].limb_phase = random_in_range(0, TRIG_MAX_ANGLE - 1);
    }
}

// ============================================================================
// ANIMATION UPDATE FUNCTIONS
// ============================================================================

static void update_vine_swing(Monkey *m) {
    // Tarzan swing: swing back, swing forward, RELEASE, fly, CATCH next vine
    int progress = (m->anim.frame * 100) / VINE_SWING_FRAMES;

    Vine *vine = &s_vines[m->anim.vine_index];
    int next_idx = m->anim.vine_index + m->direction;
    if (next_idx < 0 || next_idx >= NUM_VINES) {
        m->direction = -m->direction;
        next_idx = m->anim.vine_index + m->direction;
    }
    Vine *next_vine = &s_vines[next_idx];

    if (progress < 35) {
        // Phase 1: Swing back then forward on current vine
        int swing_p = progress * 100 / 35;
        // Swing from -30 to +45 degrees
        int32_t angle = ((swing_p * 75 / 100) - 30) * TRIG_MAX_ANGLE / 360;

        int radius = vine->length - 5;
        m->pos.x = vine->top.x + (sin_lookup(angle) * radius) / TRIG_MAX_RATIO;
        m->pos.y = vine->top.y + (cos_lookup(angle) * radius) / TRIG_MAX_RATIO;
        m->anim.rotation = angle / 6;

    } else if (progress < 65) {
        // Phase 2: RELEASE and FLY to next vine!
        int fly_p = (progress - 35) * 100 / 30;

        // Start position (release point on current vine)
        int32_t release_angle = 45 * TRIG_MAX_ANGLE / 360;
        int start_x = vine->top.x + (sin_lookup(release_angle) * (vine->length - 5)) / TRIG_MAX_RATIO;
        int start_y = vine->top.y + (cos_lookup(release_angle) * (vine->length - 5)) / TRIG_MAX_RATIO;

        // End position (catch point on next vine)
        int32_t catch_angle = -30 * TRIG_MAX_ANGLE / 360;
        int end_x = next_vine->top.x + (sin_lookup(catch_angle) * (next_vine->length - 5)) / TRIG_MAX_RATIO;
        int end_y = next_vine->top.y + (cos_lookup(catch_angle) * (next_vine->length - 5)) / TRIG_MAX_RATIO;

        // Fly in arc
        m->pos.x = start_x + (end_x - start_x) * fly_p / 100;
        int arc = (fly_p < 50) ? fly_p * 25 / 50 : (100 - fly_p) * 25 / 50;
        m->pos.y = start_y + (end_y - start_y) * fly_p / 100 - arc;

        // Arms reach forward
        m->anim.rotation = m->direction * TRIG_MAX_ANGLE / 16;

        // Switch vine at midpoint
        if (progress == 50) {
            m->anim.vine_index = next_idx;
        }

    } else {
        // Phase 3: CAUGHT! Swing forward on new vine
        int swing_p = (progress - 65) * 100 / 35;
        // Swing from -30 to +10 degrees (slowing down)
        int32_t angle = ((-30 + swing_p * 40 / 100)) * TRIG_MAX_ANGLE / 360;

        vine = &s_vines[m->anim.vine_index];
        int radius = vine->length - 5;
        m->pos.x = vine->top.x + (sin_lookup(angle) * radius) / TRIG_MAX_RATIO;
        m->pos.y = vine->top.y + (cos_lookup(angle) * radius) / TRIG_MAX_RATIO;
        m->anim.rotation = angle / 6;
    }

    m->limb_phase = progress * TRIG_MAX_ANGLE / 100;
}

static void update_climb_vine(Monkey *m) {
    // Climb up or down the vine
    int progress = (m->anim.frame * 100) / CLIMB_FRAMES;

    Vine *vine = &s_vines[m->anim.vine_index];

    // Climb direction stored in anim.rotation sign
    int climb_dir = (m->anim.target_branch > 0) ? -1 : 1;  // -1 = up, 1 = down

    // Position on vine - moves up or down
    int base_y = vine->top.y + vine->length / 2;
    int climb_range = 25;
    int offset = climb_dir * (progress - 50) * climb_range / 50;

    m->pos.x = vine->top.x;
    m->pos.y = base_y + offset;

    // Bob up and down as climbing (hand over hand)
    int bob = (sin_lookup(progress * TRIG_MAX_ANGLE / 10) * 3) / TRIG_MAX_RATIO;
    m->pos.y += bob;

    // Arms alternate
    m->limb_phase = progress * TRIG_MAX_ANGLE / 12;
    m->anim.rotation = 0;
    m->direction = 1;
}

static void update_hang_look(Monkey *m) {
    // Hang on vine and look around
    int progress = (m->anim.frame * 100) / HANG_LOOK_FRAMES;

    Vine *vine = &s_vines[m->anim.vine_index];

    // Gentle sway
    int32_t sway = (sin_lookup(progress * TRIG_MAX_ANGLE / 60) * 8) / TRIG_MAX_RATIO;

    m->pos.x = vine->top.x + sway;
    m->pos.y = vine->top.y + vine->length - 10;

    // Look left and right
    if (progress < 30) {
        m->direction = -1;
    } else if (progress < 60) {
        m->direction = 1;
    } else {
        m->direction = -1;
    }

    m->anim.rotation = sway * TRIG_MAX_ANGLE / 100;
    m->limb_phase = 0;
}

static void update_tail_hang(Monkey *m) {
    // Hang upside down by tail from branch
    int progress = (m->anim.frame * 100) / TAIL_HANG_FRAMES;

    Branch *branch = &s_branches[m->anim.branch_index];
    int mid_x = (branch->start.x + branch->end.x) / 2;
    int mid_y = (branch->start.y + branch->end.y) / 2;

    // Swing gently
    int32_t swing = (sin_lookup(progress * TRIG_MAX_ANGLE / 40) * 15) / TRIG_MAX_RATIO;

    m->pos.x = mid_x + swing;
    m->pos.y = mid_y + 22;  // Hang below branch

    m->anim.rotation = TRIG_MAX_ANGLE / 2;  // Upside down
    m->direction = (swing > 0) ? 1 : -1;
    m->limb_phase = progress * TRIG_MAX_ANGLE / 50;
}

static void update_sit_munch(Monkey *m) {
    // Sit on branch and munch an apple
    int progress = (m->anim.frame * 100) / SIT_MUNCH_FRAMES;

    Branch *branch = &s_branches[m->anim.branch_index];

    // Sit position - on top of branch
    int sit_x = branch->start.x + (branch->end.x - branch->start.x) / 3;
    if (m->direction < 0) {
        sit_x = branch->end.x - (branch->end.x - branch->start.x) / 3;
    }
    int sit_y = branch->start.y - 8;  // Sit on top of branch

    m->pos.x = sit_x;
    m->pos.y = sit_y;

    // Munch animation - hand moves to mouth
    m->limb_phase = (progress * TRIG_MAX_ANGLE / 10) % TRIG_MAX_ANGLE;
    m->anim.rotation = 0;

    // Store munch progress for drawing (how many bites taken)
    m->anim.target_branch = progress / 20;  // 0-4 bites
}

static void update_fight(Monkey *m) {
    // Fight with other monkey - chase and tussle!
    int progress = (m->anim.frame * 100) / FIGHT_FRAMES;

    // Get other monkey
    Monkey *other = (m == &s_monkeys[0]) ? &s_monkeys[1] : &s_monkeys[0];

    if (progress < 30) {
        // Phase 1: Rush toward other monkey
        int eased = ease_in_out(progress * 100 / 30);
        m->pos.x = m->anim.start_pos.x + (other->pos.x - m->anim.start_pos.x) * eased / 100;
        m->pos.y = m->anim.start_pos.y + (other->pos.y - m->anim.start_pos.y) * eased / 100;
        m->direction = (other->pos.x > m->pos.x) ? 1 : -1;

    } else if (progress < 80) {
        // Phase 2: Tussle! Both monkeys shake and tumble
        int tussle_p = (progress - 30) * 100 / 50;
        int shake_x = (sin_lookup(tussle_p * TRIG_MAX_ANGLE / 8) * 8) / TRIG_MAX_RATIO;
        int shake_y = (cos_lookup(tussle_p * TRIG_MAX_ANGLE / 6) * 5) / TRIG_MAX_RATIO;

        // Meet in middle between the two
        int meet_x = (m->anim.start_pos.x + other->anim.start_pos.x) / 2;
        int meet_y = (m->anim.start_pos.y + other->anim.start_pos.y) / 2;

        m->pos.x = meet_x + shake_x;
        m->pos.y = meet_y + shake_y;

        // Rotate during fight
        m->anim.rotation = shake_x * TRIG_MAX_ANGLE / 50;
        m->direction = (tussle_p % 20 < 10) ? 1 : -1;

    } else {
        // Phase 3: Break apart - jump back
        int retreat_p = (progress - 80) * 100 / 20;
        int eased = ease_out(retreat_p);

        int meet_x = (m->anim.start_pos.x + other->anim.start_pos.x) / 2;
        int retreat_dir = (m->anim.start_pos.x < meet_x) ? -1 : 1;

        m->pos.x = meet_x + retreat_dir * eased * 25 / 100;
        m->pos.y = SWING_ZONE_TOP + 40 - (sin_lookup(eased * TRIG_MAX_ANGLE / 200) * 15) / TRIG_MAX_RATIO;

        m->direction = -retreat_dir;  // Look back at opponent
        m->anim.rotation = 0;
    }

    m->limb_phase = progress * TRIG_MAX_ANGLE / 8;  // Fast arm movement
}

static void update_falling(Monkey *m) {
    // Monkey fell from tree! Dramatic tumble, bounce, and recovery
    int progress = (m->anim.frame * 100) / FALLING_FRAMES;

    if (progress < 40) {
        // Phase 1: Fall down tumbling with acceleration
        int fall_p = progress * 100 / 40;
        // Accelerating fall (gravity)
        int eased = (fall_p * fall_p) / 100;

        // Sideways wobble while falling
        int wobble = (sin_lookup(fall_p * TRIG_MAX_ANGLE / 8) * 20) / TRIG_MAX_RATIO;
        m->pos.x = m->anim.start_pos.x + wobble;

        // Fall to ground
        m->pos.y = m->anim.start_pos.y + (GROUND_Y - 18 - m->anim.start_pos.y) * eased / 100;

        // Tumble - multiple rotations
        m->anim.rotation = fall_p * TRIG_MAX_ANGLE / 25;  // ~4 full rotations

        // Flailing limbs frantically
        m->limb_phase = fall_p * TRIG_MAX_ANGLE / 3;

    } else if (progress < 55) {
        // Phase 2: Hit ground - BOUNCE!
        int bounce_p = (progress - 40) * 100 / 15;

        m->pos.x = m->anim.start_pos.x;

        // Big bounce up then down
        int bounce_height = 20 - (bounce_p * 20 / 100);
        m->pos.y = GROUND_Y - 18 - bounce_height;

        // Spin slowing down
        m->anim.rotation = TRIG_MAX_ANGLE / 8 - (bounce_p * TRIG_MAX_ANGLE / 800);

        m->limb_phase = bounce_p * TRIG_MAX_ANGLE / 10;

    } else if (progress < 75) {
        // Phase 3: Sitting dazed on ground with stars
        int daze_p = (progress - 55) * 100 / 20;

        m->pos.x = m->anim.start_pos.x;
        m->pos.y = GROUND_Y - 12;  // Sitting lower

        m->anim.rotation = 0;

        // Dizzy head wobble
        m->direction = (daze_p % 15 < 7) ? 1 : -1;
        m->limb_phase = daze_p * TRIG_MAX_ANGLE / 20;

    } else {
        // Phase 4: Shake it off, get up
        int recover_p = (progress - 75) * 100 / 25;

        m->pos.x = m->anim.start_pos.x;
        // Stand back up
        m->pos.y = GROUND_Y - 12 - (recover_p * 6 / 100);

        m->anim.rotation = 0;
        m->direction = 1;
        m->limb_phase = recover_p * TRIG_MAX_ANGLE / 50;
    }
}

static void trigger_fall(Monkey *m) {
    // Make monkey fall from wherever it is
    m->anim.start_pos = m->pos;
    m->anim.frame = 0;
    m->anim.current_trick = TRICK_FALLING;
    m->anim.max_frames = FALLING_FRAMES;
}

static void select_next_trick(Monkey *m) {
    m->anim.start_pos = m->pos;
    m->anim.frame = 0;
    m->anim.rotation = 0;

    // Bounce direction at edges
    if (m->anim.vine_index <= 0) m->direction = 1;
    if (m->anim.vine_index >= NUM_VINES - 1) m->direction = -1;

    int roll = random_in_range(0, 99);

    if (roll < 40) {
        // Tarzan swing to next vine!
        m->anim.current_trick = TRICK_VINE_SWING;
        m->anim.max_frames = VINE_SWING_FRAMES;
    } else if (roll < 50) {
        // Climb up/down vine
        m->anim.current_trick = TRICK_CLIMB_VINE;
        m->anim.max_frames = CLIMB_FRAMES;
        m->anim.target_branch = random_in_range(0, 1);  // 0=down, 1=up
    } else if (roll < 60) {
        // Hang and look around
        m->anim.current_trick = TRICK_HANG_LOOK;
        m->anim.max_frames = HANG_LOOK_FRAMES;
    } else if (roll < 70) {
        // Hang upside down from branch
        m->anim.current_trick = TRICK_TAIL_HANG;
        m->anim.max_frames = TAIL_HANG_FRAMES;
        m->anim.branch_index = random_in_range(0, NUM_BRANCHES - 1);
    } else if (roll < 85) {
        // Sit on branch and munch an apple
        m->anim.current_trick = TRICK_SIT_MUNCH;
        m->anim.max_frames = SIT_MUNCH_FRAMES;
        m->anim.branch_index = random_in_range(0, NUM_BRANCHES - 1);
    } else {
        // FIGHT! with the other monkey
        m->anim.current_trick = TRICK_FIGHT;
        m->anim.max_frames = FIGHT_FRAMES;
    }
}

static void update_monkey(Monkey *m) {
    m->anim.frame++;

    switch (m->anim.current_trick) {
        case TRICK_VINE_SWING:
            update_vine_swing(m);
            break;
        case TRICK_CLIMB_VINE:
            update_climb_vine(m);
            break;
        case TRICK_HANG_LOOK:
            update_hang_look(m);
            break;
        case TRICK_TAIL_HANG:
            update_tail_hang(m);
            break;
        case TRICK_SIT_MUNCH:
            update_sit_munch(m);
            break;
        case TRICK_FIGHT:
            update_fight(m);
            break;
        case TRICK_FALLING:
            update_falling(m);
            break;
        default:
            break;
    }

    // Animate tail and limbs
    m->tail_phase = (m->tail_phase + 120) % TRIG_MAX_ANGLE;
    m->limb_phase = (m->limb_phase + 200) % TRIG_MAX_ANGLE;

    // Check if trick complete
    if (m->anim.frame >= m->anim.max_frames) {
        select_next_trick(m);
    }

    // Keep in screen bounds
    if (m->pos.x < 10) m->pos.x = 10;
    if (m->pos.x > SCREEN_WIDTH - 10) m->pos.x = SCREEN_WIDTH - 10;
    if (m->pos.y < CANOPY_TOP + 15) m->pos.y = CANOPY_TOP + 15;
    if (m->pos.y > GROUND_Y - 5) m->pos.y = GROUND_Y - 5;
}

static void update_vines(void) {
    for (int i = 0; i < NUM_VINES; i++) {
        s_vines[i].sway_phase = (s_vines[i].sway_phase + 50) % TRIG_MAX_ANGLE;
    }
}

// ============================================================================
// DRAWING FUNCTIONS
// ============================================================================

static void draw_canopy(GContext *ctx) {
    // Lush tree canopy
    graphics_context_set_fill_color(ctx, COLOR_CANOPY_DARK);

    // Big overlapping circles for dense foliage
    for (int x = 0; x < SCREEN_WIDTH + 20; x += 18) {
        int y_var = (x * 7) % 10 - 5;
        graphics_fill_circle(ctx, GPoint(x, CANOPY_TOP + y_var), 22);
        graphics_fill_circle(ctx, GPoint(x + 9, CANOPY_TOP + 10 + y_var), 18);
    }

    // Lighter highlights
    graphics_context_set_fill_color(ctx, COLOR_CANOPY_LIGHT);
    for (int x = 5; x < SCREEN_WIDTH; x += 25) {
        int y_var = (x * 3) % 8;
        graphics_fill_circle(ctx, GPoint(x, CANOPY_TOP - 5 + y_var), 12);
    }

    // Bright highlights
    graphics_context_set_fill_color(ctx, COLOR_CANOPY_HIGHLIGHT);
    for (int x = 15; x < SCREEN_WIDTH; x += 35) {
        graphics_fill_circle(ctx, GPoint(x, CANOPY_TOP - 2), 6);
    }
}

static void draw_branches(GContext *ctx) {
    for (int i = 0; i < NUM_BRANCHES; i++) {
        Branch *b = &s_branches[i];

        // Branch shadow/depth
        graphics_context_set_stroke_color(ctx, COLOR_BRANCH_DARK);
        graphics_context_set_stroke_width(ctx, b->thickness + 2);
        graphics_draw_line(ctx,
            GPoint(b->start.x, b->start.y + 2),
            GPoint(b->end.x, b->end.y + 2));

        // Main branch
        graphics_context_set_stroke_color(ctx, COLOR_BRANCH);
        graphics_context_set_stroke_width(ctx, b->thickness);
        graphics_draw_line(ctx, b->start, b->end);
    }
}

static void draw_vines(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, COLOR_VINE);
    graphics_context_set_stroke_width(ctx, 2);

    for (int v = 0; v < NUM_VINES; v++) {
        Vine *vine = &s_vines[v];
        GPoint current = vine->top;
        GPoint next;

        int segments = 8;
        int seg_len = vine->length / segments;

        for (int j = 0; j < segments; j++) {
            int32_t angle = (vine->sway_phase + j * 500) % TRIG_MAX_ANGLE;
            int16_t sway = (sin_lookup(angle) * vine->sway_amount) / TRIG_MAX_RATIO;

            next.x = current.x + sway;
            next.y = current.y + seg_len;

            graphics_draw_line(ctx, current, next);
            current = next;
        }

        // Leaves along vine
        graphics_context_set_fill_color(ctx, COLOR_CANOPY_LIGHT);
        for (int j = 2; j < segments; j += 2) {
            int32_t angle = (vine->sway_phase + j * 500) % TRIG_MAX_ANGLE;
            int16_t sway = (sin_lookup(angle) * vine->sway_amount) / TRIG_MAX_RATIO;
            int leaf_x = vine->top.x + sway + ((j % 4 == 0) ? 4 : -4);
            int leaf_y = vine->top.y + j * seg_len;
            graphics_fill_circle(ctx, GPoint(leaf_x, leaf_y), 3);
        }
    }
}

static void draw_ground(GContext *ctx) {
    // Main ground
    graphics_context_set_fill_color(ctx, COLOR_GROUND);
    graphics_fill_rect(ctx, GRect(0, GROUND_Y, SCREEN_WIDTH, SCREEN_HEIGHT - GROUND_Y), 0, GCornerNone);

    // Darker top edge
    graphics_context_set_fill_color(ctx, COLOR_GROUND_DARK);
    graphics_fill_rect(ctx, GRect(0, GROUND_Y, SCREEN_WIDTH, 4), 0, GCornerNone);

    // Grass tufts
    graphics_context_set_stroke_color(ctx, COLOR_CANOPY_LIGHT);
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 14; i++) {
        int x = 5 + (i * SCREEN_WIDTH / 14);
        int h = 4 + (i % 3);
        graphics_draw_line(ctx, GPoint(x, GROUND_Y), GPoint(x - 2, GROUND_Y - h));
        graphics_draw_line(ctx, GPoint(x, GROUND_Y), GPoint(x + 2, GROUND_Y - h));
        graphics_draw_line(ctx, GPoint(x, GROUND_Y), GPoint(x, GROUND_Y - h - 1));
    }
}

static void draw_monkey_tail(GContext *ctx, Monkey *m, int16_t base_x, int16_t base_y) {
    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 2);

    GPoint current = {base_x - m->direction * 3, base_y + 5};
    GPoint next;

    // Short curly tail - just 3 segments
    for (int i = 0; i < 3; i++) {
        int32_t angle = (m->tail_phase + i * 1500) % TRIG_MAX_ANGLE;
        int16_t curl = (sin_lookup(angle) * 3) / TRIG_MAX_RATIO;

        next.x = current.x - m->direction * 3 + curl;
        next.y = current.y + 3;

        graphics_draw_line(ctx, current, next);
        current = next;
    }

    // Small curled tip
    graphics_fill_circle(ctx, current, 1);
}

static void draw_monkey(GContext *ctx, Monkey *m) {
    int16_t x = m->pos.x;
    int16_t y = m->pos.y;
    int dir = m->direction;

    // Determine pose based on what monkey is doing
    bool hanging_from_vine = (m->anim.current_trick == TRICK_VINE_SWING ||
                              m->anim.current_trick == TRICK_CLIMB_VINE ||
                              m->anim.current_trick == TRICK_HANG_LOOK);
    bool hanging_upside_down = (m->anim.current_trick == TRICK_TAIL_HANG);
    bool sitting = (m->anim.current_trick == TRICK_SIT_MUNCH);
    bool fighting = (m->anim.current_trick == TRICK_FIGHT);
    bool falling = (m->anim.current_trick == TRICK_FALLING);
    bool in_air = false;

    // Check if in flying phase of vine swing
    if (m->anim.current_trick == TRICK_VINE_SWING) {
        int progress = (m->anim.frame * 100) / VINE_SWING_FRAMES;
        if (progress >= 35 && progress < 65) {
            in_air = true;
            hanging_from_vine = false;
        }
    }

    // Calculate grip point (where hands should go)
    GPoint grip_point = {x, y - 15};  // Default above head

    if (hanging_from_vine && m->anim.vine_index >= 0 && m->anim.vine_index < NUM_VINES) {
        // Hands grip the vine - directly above the monkey
        grip_point.x = x;
        grip_point.y = y - 18;  // Just above the monkey's head
    } else if (hanging_upside_down && m->anim.branch_index >= 0) {
        // Feet grip branch above
        Branch *branch = &s_branches[m->anim.branch_index];
        grip_point.x = x;
        grip_point.y = (branch->start.y + branch->end.y) / 2;
    }

    // Draw tail first (behind body)
    if (!hanging_upside_down) {
        draw_monkey_tail(ctx, m, x, y);
    }

    // === BODY ===
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 3);

    if (hanging_upside_down) {
        // Upside down - body hangs below grip
        graphics_fill_rect(ctx, GRect(x - 5, y - 6, 10, 12), 3, GCornersAll);
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
        graphics_fill_rect(ctx, GRect(x - 3, y - 4, 6, 8), 2, GCornersAll);

        // LEGS GO UP to grip the branch!
        graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
        graphics_draw_line(ctx, GPoint(x - 3, y - 6), GPoint(grip_point.x - 3, grip_point.y));
        graphics_draw_line(ctx, GPoint(x + 3, y - 6), GPoint(grip_point.x + 3, grip_point.y));
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_circle(ctx, GPoint(grip_point.x - 3, grip_point.y), 2);
        graphics_fill_circle(ctx, GPoint(grip_point.x + 3, grip_point.y), 2);

        // Arms dangle down
        int arm_dangle = (sin_lookup(m->limb_phase) * 3) / TRIG_MAX_RATIO;
        graphics_draw_line(ctx, GPoint(x - 5, y + 4), GPoint(x - 7 + arm_dangle, y + 12));
        graphics_draw_line(ctx, GPoint(x + 5, y + 4), GPoint(x + 7 - arm_dangle, y + 12));
        graphics_fill_circle(ctx, GPoint(x - 7 + arm_dangle, y + 12), 2);
        graphics_fill_circle(ctx, GPoint(x + 7 - arm_dangle, y + 12), 2);

    } else if (hanging_from_vine) {
        // Hanging from vine - body swings below grip point

        // Draw the vine going UP from grip point to canopy
        graphics_context_set_stroke_color(ctx, COLOR_VINE);
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(ctx, GPoint(x, y - 16), GPoint(x, CANOPY_TOP + 10));

        // Body
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_rect(ctx, GRect(x - 5, y - 5, 10, 12), 3, GCornersAll);
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
        graphics_fill_rect(ctx, GRect(x - 3, y - 2, 6, 8), 2, GCornersAll);

        // Arms reaching UP to grip vine
        graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(ctx, GPoint(x - 4, y - 5), GPoint(x, y - 16));
        graphics_draw_line(ctx, GPoint(x + 4, y - 5), GPoint(x, y - 16));
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_circle(ctx, GPoint(x, y - 16), 3);  // Hands gripping

        // Legs dangle and swing with momentum
        int leg_offset = (m->anim.rotation * 8) / TRIG_MAX_RATIO;
        graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - 5 - leg_offset, y + 15));
        graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + 5 - leg_offset, y + 15));
        graphics_fill_circle(ctx, GPoint(x - 5 - leg_offset, y + 15), 2);
        graphics_fill_circle(ctx, GPoint(x + 5 - leg_offset, y + 15), 2);

    } else if (sitting) {
        // Sitting on branch munching apple
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_rect(ctx, GRect(x - 5, y - 3, 10, 10), 3, GCornersAll);
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
        graphics_fill_rect(ctx, GRect(x - 3, y - 1, 6, 7), 2, GCornersAll);

        // Legs tucked under, sitting
        graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(ctx, GPoint(x - 4, y + 7), GPoint(x - 6, y + 5));
        graphics_draw_line(ctx, GPoint(x + 4, y + 7), GPoint(x + 6, y + 5));
        graphics_fill_circle(ctx, GPoint(x - 6, y + 5), 2);
        graphics_fill_circle(ctx, GPoint(x + 6, y + 5), 2);

        // One arm holds apple to mouth (animates)
        int munch_phase = (sin_lookup(m->limb_phase) * 4) / TRIG_MAX_RATIO;
        int apple_x = x + dir * 6;
        int apple_y = y - 8 + munch_phase;  // Moves to mouth

        // Other arm rests
        graphics_draw_line(ctx, GPoint(x - dir * 5, y), GPoint(x - dir * 8, y + 5));
        graphics_fill_circle(ctx, GPoint(x - dir * 8, y + 5), 2);

        // Arm holding apple
        graphics_draw_line(ctx, GPoint(x + dir * 5, y - 2), GPoint(apple_x, apple_y + 3));
        graphics_fill_circle(ctx, GPoint(apple_x, apple_y + 3), 2);

        // Draw apple (gets smaller as eaten)
        int bites = m->anim.target_branch;  // 0-4
        int apple_radius = 5 - bites;
        if (apple_radius > 1) {
            graphics_context_set_fill_color(ctx, COLOR_APPLE);
            graphics_fill_circle(ctx, GPoint(apple_x, apple_y), apple_radius);
            // Bite mark
            if (bites > 0) {
                graphics_context_set_fill_color(ctx, COLOR_APPLE_BITE);
                graphics_fill_circle(ctx, GPoint(apple_x - dir * 2, apple_y), bites);
            }
            // Apple stem
            graphics_context_set_stroke_color(ctx, COLOR_BRANCH);
            graphics_context_set_stroke_width(ctx, 1);
            graphics_draw_line(ctx, GPoint(apple_x, apple_y - apple_radius),
                              GPoint(apple_x + 1, apple_y - apple_radius - 2));
        }

    } else if (fighting) {
        // Fighting pose - arms out, aggressive
        int fight_progress = (m->anim.frame * 100) / FIGHT_FRAMES;
        bool tussling = (fight_progress >= 30 && fight_progress < 80);

        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_rect(ctx, GRect(x - 5, y - 5, 10, 12), 3, GCornersAll);
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
        graphics_fill_rect(ctx, GRect(x - 3, y - 2, 6, 8), 2, GCornersAll);

        graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
        graphics_context_set_stroke_width(ctx, 3);

        if (tussling) {
            // Arms swinging wildly
            int arm_swing = (sin_lookup(m->limb_phase * 3) * 10) / TRIG_MAX_RATIO;
            graphics_draw_line(ctx, GPoint(x - 5, y - 2), GPoint(x - 12 + arm_swing, y - 8));
            graphics_draw_line(ctx, GPoint(x + 5, y - 2), GPoint(x + 12 - arm_swing, y - 8));
            graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
            graphics_fill_circle(ctx, GPoint(x - 12 + arm_swing, y - 8), 2);
            graphics_fill_circle(ctx, GPoint(x + 12 - arm_swing, y - 8), 2);
        } else {
            // Arms ready to fight
            graphics_draw_line(ctx, GPoint(x - 5, y - 2), GPoint(x - 10, y - 6));
            graphics_draw_line(ctx, GPoint(x + 5, y - 2), GPoint(x + 10, y - 6));
            graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
            graphics_fill_circle(ctx, GPoint(x - 10, y - 6), 2);
            graphics_fill_circle(ctx, GPoint(x + 10, y - 6), 2);
        }

        // Legs braced
        graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - 7, y + 14));
        graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + 7, y + 14));
        graphics_fill_circle(ctx, GPoint(x - 7, y + 14), 2);
        graphics_fill_circle(ctx, GPoint(x + 7, y + 14), 2);

    } else if (falling) {
        // Falling pose - tumbling with limbs flailing
        int fall_progress = (m->anim.frame * 100) / FALLING_FRAMES;

        // Apply rotation to body position for tumbling effect
        int rot_offset_x = (sin_lookup(m->anim.rotation) * 3) / TRIG_MAX_RATIO;
        int rot_offset_y = (cos_lookup(m->anim.rotation) * 2) / TRIG_MAX_RATIO;

        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_rect(ctx, GRect(x - 5 + rot_offset_x, y - 5 + rot_offset_y, 10, 12), 3, GCornersAll);
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
        graphics_fill_rect(ctx, GRect(x - 3 + rot_offset_x, y - 2 + rot_offset_y, 6, 8), 2, GCornersAll);

        // Flailing arms and legs
        graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
        graphics_context_set_stroke_width(ctx, 3);

        int flail = (sin_lookup(m->limb_phase) * 12) / TRIG_MAX_RATIO;
        int flail2 = (cos_lookup(m->limb_phase) * 10) / TRIG_MAX_RATIO;

        // Arms flailing wildly
        graphics_draw_line(ctx, GPoint(x - 5, y - 2), GPoint(x - 10 + flail, y - 8 + flail2));
        graphics_draw_line(ctx, GPoint(x + 5, y - 2), GPoint(x + 10 - flail, y - 6 - flail2));
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_circle(ctx, GPoint(x - 10 + flail, y - 8 + flail2), 2);
        graphics_fill_circle(ctx, GPoint(x + 10 - flail, y - 6 - flail2), 2);

        // Legs kicking
        graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - 8 - flail2, y + 14 + flail));
        graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + 8 + flail2, y + 12 - flail));
        graphics_fill_circle(ctx, GPoint(x - 8 - flail2, y + 14 + flail), 2);
        graphics_fill_circle(ctx, GPoint(x + 8 + flail2, y + 12 - flail), 2);

        // Draw stars if landed (dizzy)
        if (fall_progress >= 60) {
            graphics_context_set_fill_color(ctx, GColorYellow);
            int star_phase = fall_progress * 5;
            for (int i = 0; i < 3; i++) {
                int star_angle = (star_phase + i * TRIG_MAX_ANGLE / 3) % TRIG_MAX_ANGLE;
                int star_x = x + (sin_lookup(star_angle) * 12) / TRIG_MAX_RATIO;
                int star_y = y - 18 + (cos_lookup(star_angle) * 5) / TRIG_MAX_RATIO;
                graphics_fill_circle(ctx, GPoint(star_x, star_y), 2);
            }
        }

    } else {
        // In air or on ground - normal pose
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_rect(ctx, GRect(x - 5, y - 5, 10, 12), 3, GCornersAll);
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
        graphics_fill_rect(ctx, GRect(x - 3, y - 2, 6, 8), 2, GCornersAll);

        // Arms and legs spread for balance in air, or tucked
        graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
        graphics_context_set_stroke_width(ctx, 3);
        int spread = in_air ? 8 : 5;
        graphics_draw_line(ctx, GPoint(x - 5, y), GPoint(x - spread, y - 2));
        graphics_draw_line(ctx, GPoint(x + 5, y), GPoint(x + spread, y - 2));
        graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
        graphics_fill_circle(ctx, GPoint(x - spread, y - 2), 2);
        graphics_fill_circle(ctx, GPoint(x + spread, y - 2), 2);

        graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - spread + 2, y + 12));
        graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + spread - 2, y + 12));
        graphics_fill_circle(ctx, GPoint(x - spread + 2, y + 12), 2);
        graphics_fill_circle(ctx, GPoint(x + spread - 2, y + 12), 2);
    }

    // === HEAD ===
    int head_y = hanging_upside_down ? y + 12 : y - 10;

    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_circle(ctx, GPoint(x, head_y), 7);

    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FACE);
    graphics_fill_circle(ctx, GPoint(x + dir * 2, head_y + (hanging_upside_down ? -1 : 1)), 5);

    // Ears
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_circle(ctx, GPoint(x - 6, head_y), 3);
    graphics_fill_circle(ctx, GPoint(x + 6, head_y), 3);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FACE);
    graphics_fill_circle(ctx, GPoint(x - 6, head_y), 1);
    graphics_fill_circle(ctx, GPoint(x + 6, head_y), 1);

    // Eyes
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_DARK);
    int eye_y = head_y + (hanging_upside_down ? 2 : -2);
    graphics_fill_circle(ctx, GPoint(x + dir * 1, eye_y), 1);
    graphics_fill_circle(ctx, GPoint(x + dir * 4, eye_y), 1);

    // Mouth
    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_DARK);
    graphics_context_set_stroke_width(ctx, 1);
    int mouth_y = head_y + (hanging_upside_down ? -3 : 3);
    graphics_draw_line(ctx, GPoint(x + dir * 1, mouth_y), GPoint(x + dir * 4, mouth_y));

    // Tail in front if upside down
    if (hanging_upside_down) {
        draw_monkey_tail(ctx, m, x, y);
    }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    // 1. Sky gradient
    graphics_context_set_fill_color(ctx, COLOR_SKY);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_WIDTH, CANOPY_TOP + 20), 0, GCornerNone);

    // Lower sky slightly different
    graphics_context_set_fill_color(ctx, COLOR_SKY_LOW);
    graphics_fill_rect(ctx, GRect(0, CANOPY_TOP + 20, SCREEN_WIDTH, GROUND_Y - CANOPY_TOP - 20), 0, GCornerNone);

    // 2. Tree canopy (background)
    draw_canopy(ctx);

    // 3. Branches
    draw_branches(ctx);

    // 4. Vines
    draw_vines(ctx);

    // 5. Monkeys!
    for (int i = 0; i < NUM_MONKEYS; i++) {
        if (s_monkeys[i].active) {
            draw_monkey(ctx, &s_monkeys[i]);
        }
    }

    // 6. Ground
    draw_ground(ctx);

    // 7. Battery indicator (top right corner)
    int batt_x = SCREEN_WIDTH - 28;
    int batt_y = 4;
    int batt_width = 22;
    int batt_height = 10;

    // Battery outline
    graphics_context_set_stroke_color(ctx, COLOR_TIME_TEXT);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, GRect(batt_x, batt_y, batt_width, batt_height));

    // Battery nub
    graphics_context_set_fill_color(ctx, COLOR_TIME_TEXT);
    graphics_fill_rect(ctx, GRect(batt_x + batt_width, batt_y + 3, 2, 4), 0, GCornerNone);

    // Battery fill based on level
    int fill_width = (s_battery_level * (batt_width - 4)) / 100;
    if (fill_width < 2) fill_width = 2;

    // Color based on battery level
#ifdef PBL_COLOR
    if (s_battery_level <= 20) {
        graphics_context_set_fill_color(ctx, GColorRed);
    } else if (s_battery_level <= 40) {
        graphics_context_set_fill_color(ctx, GColorOrange);
    } else {
        graphics_context_set_fill_color(ctx, GColorGreen);
    }
#else
    graphics_context_set_fill_color(ctx, COLOR_TIME_TEXT);
#endif
    graphics_fill_rect(ctx, GRect(batt_x + 2, batt_y + 2, fill_width, batt_height - 4), 0, GCornerNone);
}

// ============================================================================
// ANIMATION TIMER
// ============================================================================

static void animation_timer_callback(void *data) {
    update_vines();

    for (int i = 0; i < NUM_MONKEYS; i++) {
        if (s_monkeys[i].active) {
            update_monkey(&s_monkeys[i]);
        }
    }

    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }

    uint32_t interval = (s_battery_level <= LOW_BATTERY_THRESHOLD)
                        ? ANIMATION_INTERVAL_LOW_POWER
                        : ANIMATION_INTERVAL;
    s_animation_timer = app_timer_register(interval, animation_timer_callback, NULL);
}

// ============================================================================
// TIME UPDATE
// ============================================================================

static void update_time(void) {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    // Update time
    strftime(s_time_buffer, sizeof(s_time_buffer),
             clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
    text_layer_set_text(s_time_layer, s_time_buffer);

    // Update date (e.g., "Sat Jan 18")
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
    text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
}

static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
    // Redraw to update battery indicator
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

// ============================================================================
// SHAKE HANDLER - Monkeys fall when you shake the watch!
// ============================================================================

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    // Shake detected! Make all monkeys fall from the trees!
    bool any_fell = false;
    for (int i = 0; i < NUM_MONKEYS; i++) {
        if (s_monkeys[i].active && s_monkeys[i].anim.current_trick != TRICK_FALLING) {
            trigger_fall(&s_monkeys[i]);
            any_fell = true;
        }
    }

    // Haptic feedback - double vibe when monkeys fall!
    if (any_fell) {
        vibes_double_pulse();
    }

    // Immediate redraw
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

// ============================================================================
// WINDOW HANDLERS
// ============================================================================

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    srand(time(NULL));

    init_vines();
    init_branches();
    init_monkeys();

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    s_time_layer = text_layer_create(GRect(0, TIME_Y, bounds.size.w, 38));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, COLOR_TIME_TEXT);
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

    // Date layer (day and date below time)
    s_date_layer = text_layer_create(GRect(0, DATE_Y, bounds.size.w, 20));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, COLOR_TIME_TEXT);
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

    update_time();
    battery_callback(battery_state_service_peek());

    s_animation_timer = app_timer_register(ANIMATION_INTERVAL, animation_timer_callback, NULL);
}

static void main_window_unload(Window *window) {
    if (s_animation_timer) {
        app_timer_cancel(s_animation_timer);
        s_animation_timer = NULL;
    }

    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);
    layer_destroy(s_canvas_layer);
}

// ============================================================================
// APP LIFECYCLE
// ============================================================================

static void init(void) {
    s_main_window = window_create();

    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });

    window_stack_push(s_main_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
    accel_tap_service_subscribe(accel_tap_handler);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    accel_tap_service_unsubscribe();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
