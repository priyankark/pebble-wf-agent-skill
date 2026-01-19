#include <pebble.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define ANIMATION_INTERVAL 100
#define ANIMATION_INTERVAL_LOW_POWER 200
#define LOW_BATTERY_THRESHOLD 20

#define NUM_MONKEYS 2
#define NUM_VINES 4
#define NUM_BRANCHES 2

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
  #define COLOR_STAR GColorYellow
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
  #define COLOR_STAR GColorBlack
#endif

// ============================================================================
// TRICK TYPES
// ============================================================================

typedef enum {
  TRICK_VINE_SWING,
  TRICK_CLIMB_VINE,
  TRICK_HANG_LOOK,
  TRICK_TAIL_HANG,
  TRICK_SIT_MUNCH,
  TRICK_FIGHT,
  TRICK_FALLING,
  TRICK_COUNT
} TrickType;

// Frame counts (at ~20 FPS)
#define VINE_SWING_FRAMES 50
#define CLIMB_FRAMES 40
#define HANG_LOOK_FRAMES 60
#define TAIL_HANG_FRAMES 50
#define SIT_MUNCH_FRAMES 80
#define FIGHT_FRAMES 60
#define FALLING_FRAMES 50

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
  int32_t limb_phase;
  bool active;
} Monkey;

typedef struct {
  GPoint top;
  int16_t length;
  int32_t sway_phase;
  int16_t sway_amount;
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
static bool s_running = false;         // app active + window loaded
static bool s_window_loaded = false;   // window lifecycle guard
static bool s_fully_initialized = false; // set only after everything is ready
static bool s_in_focus = true;         // track focus state separately
static void focus_handler(bool in_focus);
static bool s_bt_connected = true;     // phone connection (pause when false)
static bool s_is_charging = false;     // charging state (pause when true)
static bool s_low_power_mode = false;  // user FPS cap toggle
static bool s_vibes_enabled = true;    // user-toggle vibrations

enum {
  PERSIST_KEY_LOW_POWER = 1,
  PERSIST_KEY_VIBES = 2,
};

static inline bool should_animate(void);
static uint32_t get_animation_interval(void);
static void ensure_timer_running(void);
static void bt_handler(bool connected);
static void click_config_provider(void *context);
static void up_click_handler(ClickRecognizerRef recognizer, void *context);
static void down_click_handler(ClickRecognizerRef recognizer, void *context);

static Monkey s_monkeys[NUM_MONKEYS];
static Vine s_vines[NUM_VINES];
static Branch s_branches[NUM_BRANCHES];

static int s_battery_level = 100;
static char s_time_buffer[8];
static char s_date_buffer[16];
static uint32_t s_last_shake_time = 0;  // cooldown for shake detection

// Shake detection threshold (magnitude squared to avoid sqrt)
// Gravity is ~1000 per axis, so normal is ~1000000. Vigorous shake > 3000000
#define SHAKE_THRESHOLD_SQ 4000000
#define SHAKE_COOLDOWN_MS 1500

// ============================================================================
// SAFE MATH HELPERS (CRITICAL FOR PEBBLE STABILITY)
// ============================================================================

// Pebble trig lookups are only safe in [0, TRIG_MAX_ANGLE).
// Using & (TRIG_MAX_ANGLE-1) works because TRIG_MAX_ANGLE is power-of-two (65536).
#define ANGLE_MASK (TRIG_MAX_ANGLE - 1)

static inline int32_t sin_safe(int32_t angle) {
  return sin_lookup(angle & ANGLE_MASK);
}

static inline int32_t cos_safe(int32_t angle) {
  return cos_lookup(angle & ANGLE_MASK);
}

static int random_in_range(int min, int max) {
  if (max <= min) return min;
  return min + (rand() % (max - min + 1));
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int ease_in_out(int progress) {
  progress = clampi(progress, 0, 100);
  int32_t angle = (progress * TRIG_MAX_ANGLE / 2) / 100;
  int result = 50 - (cos_safe(angle) * 50) / TRIG_MAX_RATIO;
  return result;
}

static int ease_out(int progress) {
  progress = clampi(progress, 0, 100);
  return 100 - ((100 - progress) * (100 - progress)) / 100;
}

// ============================================================================
// ANIMATION CONTROL HELPERS (EFFICIENCY)
// ============================================================================

static inline bool should_animate(void) {
  return s_fully_initialized && s_running && s_window_loaded && s_in_focus &&
         s_canvas_layer && s_bt_connected && !s_is_charging;
}

static uint32_t get_animation_interval(void) {
  uint32_t interval = ANIMATION_INTERVAL;        // ~10 FPS
#ifndef PBL_COLOR
  interval = ANIMATION_INTERVAL_LOW_POWER;       // ~5 FPS on B/W
#endif
  if (s_low_power_mode || s_battery_level <= LOW_BATTERY_THRESHOLD) {
    interval = ANIMATION_INTERVAL_LOW_POWER;
  }
  return interval;
}

// Forward declaration
static void animation_timer_callback(void *data);

static void ensure_timer_running(void) {
  if (should_animate()) {
    if (!s_animation_timer) {
      uint32_t interval = get_animation_interval();
      if (interval < 50) interval = 50;  // Minimum 50ms to prevent overload
      s_animation_timer = app_timer_register(interval, animation_timer_callback, NULL);
    }
  } else {
    if (s_animation_timer) {
      app_timer_cancel(s_animation_timer);
      s_animation_timer = NULL;
    }
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

#if NUM_VINES < 2
  #error "NUM_VINES must be at least 2 to avoid division by zero"
#endif

static void init_vines(void) {
  for (int i = 0; i < NUM_VINES; i++) {
    s_vines[i].top.x = 15 + (i * (SCREEN_WIDTH - 30) / (NUM_VINES - 1));
    s_vines[i].top.y = CANOPY_TOP - 5;
    s_vines[i].length = clampi(random_in_range(35, 50), 20, 70);
    s_vines[i].sway_phase = random_in_range(0, TRIG_MAX_ANGLE - 1);
    s_vines[i].sway_amount = random_in_range(5, 10);
  }
}

static void init_branches(void) {
  s_branches[0] = (Branch){
    .start = {10, CANOPY_TOP + 12},
    .end = {SCREEN_WIDTH / 2, CANOPY_TOP + 16},
    .thickness = 4
  };
  s_branches[1] = (Branch){
    .start = {SCREEN_WIDTH / 2 + 10, CANOPY_TOP + 10},
    .end = {SCREEN_WIDTH - 10, CANOPY_TOP + 14},
    .thickness = 5
  };
}

static void select_next_trick(Monkey *m);

static void init_monkeys(void) {
  for (int i = 0; i < NUM_MONKEYS; i++) {
    s_monkeys[i].direction = (i == 0) ? 1 : -1;
    s_monkeys[i].active = true;

    if (i == 0) {
      s_monkeys[i].pos.x = SCREEN_WIDTH / 3;
      s_monkeys[i].pos.y = SWING_ZONE_TOP + 15;
      s_monkeys[i].anim.vine_index = 1;
    } else {
      s_monkeys[i].pos.x = 2 * SCREEN_WIDTH / 3;
      s_monkeys[i].pos.y = SWING_ZONE_TOP + 25;
      s_monkeys[i].anim.vine_index = 2;
    }

    // ✅ CRITICAL: initialize ALL anim fields to prevent garbage reads/crashes
    s_monkeys[i].anim.current_trick = TRICK_VINE_SWING;
    s_monkeys[i].anim.frame = random_in_range(0, 20);
    s_monkeys[i].anim.max_frames = VINE_SWING_FRAMES;
    s_monkeys[i].anim.rotation = 0;
    s_monkeys[i].anim.branch_index = 0;
    s_monkeys[i].anim.target_branch = 0;
    s_monkeys[i].anim.start_pos = s_monkeys[i].pos;
    s_monkeys[i].anim.end_pos = s_monkeys[i].pos;

    s_monkeys[i].tail_phase = random_in_range(0, TRIG_MAX_ANGLE - 1);
    s_monkeys[i].limb_phase = random_in_range(0, TRIG_MAX_ANGLE - 1);
  }
}

// ============================================================================
// ANIMATION UPDATERS
// ============================================================================

static void update_vine_swing(Monkey *m) {
  int progress = (m->anim.frame * 100) / VINE_SWING_FRAMES;
  progress = clampi(progress, 0, 100);

  if (m->anim.vine_index < 0) m->anim.vine_index = 0;
  if (m->anim.vine_index >= NUM_VINES) m->anim.vine_index = NUM_VINES - 1;

  Vine *vine = &s_vines[m->anim.vine_index];

  int next_idx = m->anim.vine_index + m->direction;
  if (next_idx < 0 || next_idx >= NUM_VINES) {
    m->direction = -m->direction;
    next_idx = m->anim.vine_index + m->direction;
  }
  if (next_idx < 0) next_idx = 0;
  if (next_idx >= NUM_VINES) next_idx = NUM_VINES - 1;

  Vine *next_vine = &s_vines[next_idx];

  if (progress < 35) {
    int swing_p = (progress * 100) / 35;
    int32_t angle = ((swing_p * 75 / 100) - 30) * TRIG_MAX_ANGLE / 360;

    int radius = vine->length - 5;
    m->pos.x = vine->top.x + (sin_safe(angle) * radius) / TRIG_MAX_RATIO;
    m->pos.y = vine->top.y + (cos_safe(angle) * radius) / TRIG_MAX_RATIO;
    m->anim.rotation = angle / 6;

  } else if (progress < 65) {
    int fly_p = (progress - 35) * 100 / 30;
    fly_p = clampi(fly_p, 0, 100);

    int32_t release_angle = 45 * TRIG_MAX_ANGLE / 360;
    int start_x = vine->top.x + (sin_safe(release_angle) * (vine->length - 5)) / TRIG_MAX_RATIO;
    int start_y = vine->top.y + (cos_safe(release_angle) * (vine->length - 5)) / TRIG_MAX_RATIO;

    // ✅ negative angles are safe now due to sin_safe/cos_safe
    int32_t catch_angle = (-30) * TRIG_MAX_ANGLE / 360;
    int end_x = next_vine->top.x + (sin_safe(catch_angle) * (next_vine->length - 5)) / TRIG_MAX_RATIO;
    int end_y = next_vine->top.y + (cos_safe(catch_angle) * (next_vine->length - 5)) / TRIG_MAX_RATIO;

    m->pos.x = start_x + (end_x - start_x) * fly_p / 100;

    int arc = (fly_p < 50) ? (fly_p * 25 / 50) : ((100 - fly_p) * 25 / 50);
    m->pos.y = start_y + (end_y - start_y) * fly_p / 100 - arc;

    m->anim.rotation = m->direction * TRIG_MAX_ANGLE / 16;

    // ✅ correct mid-flight vine switch (instead of progress == 50)
    int mid_frame = (VINE_SWING_FRAMES * 50) / 100;
    if (m->anim.frame == mid_frame) {
      m->anim.vine_index = next_idx;
    }

  } else {
    int swing_p = (progress - 65) * 100 / 35;
    swing_p = clampi(swing_p, 0, 100);

    int32_t angle = ((-30 + swing_p * 40 / 100)) * TRIG_MAX_ANGLE / 360;

    vine = &s_vines[m->anim.vine_index];
    int radius = vine->length - 5;
    m->pos.x = vine->top.x + (sin_safe(angle) * radius) / TRIG_MAX_RATIO;
    m->pos.y = vine->top.y + (cos_safe(angle) * radius) / TRIG_MAX_RATIO;
    m->anim.rotation = angle / 6;
  }

  m->limb_phase = progress * TRIG_MAX_ANGLE / 100;
}

static void update_climb_vine(Monkey *m) {
  int progress = (m->anim.frame * 100) / CLIMB_FRAMES;
  progress = clampi(progress, 0, 100);

  if (m->anim.vine_index < 0) m->anim.vine_index = 0;
  if (m->anim.vine_index >= NUM_VINES) m->anim.vine_index = NUM_VINES - 1;

  Vine *vine = &s_vines[m->anim.vine_index];

  int climb_dir = (m->anim.target_branch > 0) ? -1 : 1;

  int base_y = vine->top.y + vine->length / 2;
  int climb_range = 25;
  int offset = climb_dir * (progress - 50) * climb_range / 50;

  m->pos.x = vine->top.x;
  m->pos.y = base_y + offset;

  int bob = (sin_safe(progress * TRIG_MAX_ANGLE / 10) * 3) / TRIG_MAX_RATIO;
  m->pos.y += bob;

  m->limb_phase = progress * TRIG_MAX_ANGLE / 12;
  m->anim.rotation = 0;
  m->direction = 1;
}

static void update_hang_look(Monkey *m) {
  int progress = (m->anim.frame * 100) / HANG_LOOK_FRAMES;
  progress = clampi(progress, 0, 100);

  if (m->anim.vine_index < 0) m->anim.vine_index = 0;
  if (m->anim.vine_index >= NUM_VINES) m->anim.vine_index = NUM_VINES - 1;

  Vine *vine = &s_vines[m->anim.vine_index];

  int32_t sway = (sin_safe(progress * TRIG_MAX_ANGLE / 60) * 8) / TRIG_MAX_RATIO;

  m->pos.x = vine->top.x + sway;
  m->pos.y = vine->top.y + vine->length - 10;

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
  int progress = (m->anim.frame * 100) / TAIL_HANG_FRAMES;
  progress = clampi(progress, 0, 100);

  if (m->anim.branch_index < 0) m->anim.branch_index = 0;
  if (m->anim.branch_index >= NUM_BRANCHES) m->anim.branch_index = NUM_BRANCHES - 1;

  Branch *branch = &s_branches[m->anim.branch_index];
  int mid_x = (branch->start.x + branch->end.x) / 2;
  int mid_y = (branch->start.y + branch->end.y) / 2;

  int32_t swing = (sin_safe(progress * TRIG_MAX_ANGLE / 40) * 15) / TRIG_MAX_RATIO;

  m->pos.x = mid_x + swing;
  m->pos.y = mid_y + 22;

  m->anim.rotation = TRIG_MAX_ANGLE / 2;
  m->direction = (swing > 0) ? 1 : -1;
  m->limb_phase = progress * TRIG_MAX_ANGLE / 50;
}

static void update_sit_munch(Monkey *m) {
  int progress = (m->anim.frame * 100) / SIT_MUNCH_FRAMES;
  progress = clampi(progress, 0, 100);

  if (m->anim.branch_index < 0) m->anim.branch_index = 0;
  if (m->anim.branch_index >= NUM_BRANCHES) m->anim.branch_index = NUM_BRANCHES - 1;

  Branch *branch = &s_branches[m->anim.branch_index];

  int sit_x = branch->start.x + (branch->end.x - branch->start.x) / 3;
  if (m->direction < 0) {
    sit_x = branch->end.x - (branch->end.x - branch->start.x) / 3;
  }
  int sit_y = branch->start.y - 8;

  m->pos.x = sit_x;
  m->pos.y = sit_y;

  m->limb_phase = (progress * TRIG_MAX_ANGLE / 10) % TRIG_MAX_ANGLE;
  m->anim.rotation = 0;

  m->anim.target_branch = clampi(progress / 20, 0, 4); // bites 0..4
}

static void update_fight(Monkey *m) {
  int progress = (m->anim.frame * 100) / FIGHT_FRAMES;
  progress = clampi(progress, 0, 100);

  // Use start_pos for stable reference - avoid feedback loop with other monkey
  int center_x = SCREEN_WIDTH / 2;
  int center_y = SWING_ZONE_TOP + 30;

  if (progress < 30) {
    int eased = ease_in_out(progress * 100 / 30);
    m->pos.x = m->anim.start_pos.x + (center_x - m->anim.start_pos.x) * eased / 100;
    m->pos.y = m->anim.start_pos.y + (center_y - m->anim.start_pos.y) * eased / 100;
    m->direction = (center_x > m->anim.start_pos.x) ? 1 : -1;

  } else if (progress < 80) {
    int tussle_p = (progress - 30) * 100 / 50;
    tussle_p = clampi(tussle_p, 0, 100);

    int shake_x = (sin_safe(tussle_p * TRIG_MAX_ANGLE / 8) * 8) / TRIG_MAX_RATIO;
    int shake_y = (cos_safe(tussle_p * TRIG_MAX_ANGLE / 6) * 5) / TRIG_MAX_RATIO;

    // Use fixed center point to avoid feedback loop
    m->pos.x = center_x + shake_x;
    m->pos.y = center_y + shake_y;

    m->anim.rotation = shake_x * TRIG_MAX_ANGLE / 50;
    m->direction = (tussle_p % 20 < 10) ? 1 : -1;

  } else {
    int retreat_p = (progress - 80) * 100 / 20;
    retreat_p = clampi(retreat_p, 0, 100);
    int eased = ease_out(retreat_p);

    int retreat_dir = (m->anim.start_pos.x < center_x) ? -1 : 1;

    m->pos.x = center_x + retreat_dir * eased * 25 / 100;
    m->pos.y = SWING_ZONE_TOP + 40 - (sin_safe(eased * TRIG_MAX_ANGLE / 200) * 15) / TRIG_MAX_RATIO;

    m->direction = -retreat_dir;
    m->anim.rotation = 0;
  }

  m->limb_phase = progress * TRIG_MAX_ANGLE / 8;
}

static void update_falling(Monkey *m) {
  int progress = (m->anim.frame * 100) / FALLING_FRAMES;
  progress = clampi(progress, 0, 100);

  if (progress < 40) {
    int fall_p = progress * 100 / 40;
    fall_p = clampi(fall_p, 0, 100);
    int eased = (fall_p * fall_p) / 100;

    int wobble = (sin_safe(fall_p * TRIG_MAX_ANGLE / 8) * 20) / TRIG_MAX_RATIO;
    m->pos.x = m->anim.start_pos.x + wobble;

    m->pos.y = m->anim.start_pos.y + (GROUND_Y - 18 - m->anim.start_pos.y) * eased / 100;

    m->anim.rotation = fall_p * TRIG_MAX_ANGLE / 25;
    m->limb_phase = fall_p * TRIG_MAX_ANGLE / 3;

  } else if (progress < 55) {
    int bounce_p = (progress - 40) * 100 / 15;
    bounce_p = clampi(bounce_p, 0, 100);

    m->pos.x = m->anim.start_pos.x;

    int bounce_height = 20 - (bounce_p * 20 / 100);
    m->pos.y = GROUND_Y - 18 - bounce_height;

    m->anim.rotation = TRIG_MAX_ANGLE / 8 - (bounce_p * TRIG_MAX_ANGLE / 800);
    m->limb_phase = bounce_p * TRIG_MAX_ANGLE / 10;

  } else if (progress < 75) {
    int daze_p = (progress - 55) * 100 / 20;
    daze_p = clampi(daze_p, 0, 100);

    m->pos.x = m->anim.start_pos.x;
    m->pos.y = GROUND_Y - 12;

    m->anim.rotation = 0;
    m->direction = (daze_p % 15 < 7) ? 1 : -1;
    m->limb_phase = daze_p * TRIG_MAX_ANGLE / 20;

  } else {
    int recover_p = (progress - 75) * 100 / 25;
    recover_p = clampi(recover_p, 0, 100);

    m->pos.x = m->anim.start_pos.x;
    m->pos.y = GROUND_Y - 12 - (recover_p * 6 / 100);

    m->anim.rotation = 0;
    m->direction = 1;
    m->limb_phase = recover_p * TRIG_MAX_ANGLE / 50;
  }
}

static void trigger_fall(Monkey *m) {
  if (!m) return;
  m->anim.start_pos = m->pos;
  m->anim.frame = 0;
  m->anim.current_trick = TRICK_FALLING;
  m->anim.max_frames = FALLING_FRAMES;
  m->anim.rotation = 0;
}

static void select_next_trick(Monkey *m) {
  if (!m) return;

  // If recovering from a fall, reset to a valid vine position
  bool was_falling = (m->anim.current_trick == TRICK_FALLING);
  if (was_falling) {
    // Pick a random vine and reset position to it
    m->anim.vine_index = random_in_range(0, NUM_VINES - 1);
    Vine *vine = &s_vines[m->anim.vine_index];
    m->pos.x = vine->top.x;
    m->pos.y = vine->top.y + vine->length - 10;
    m->direction = random_in_range(0, 1) ? 1 : -1;
  }

  m->anim.start_pos = m->pos;
  m->anim.frame = 0;
  m->anim.rotation = 0;

  // Ensure vine_index is valid
  if (m->anim.vine_index < 0) m->anim.vine_index = 0;
  if (m->anim.vine_index >= NUM_VINES) m->anim.vine_index = NUM_VINES - 1;

  // Ensure branch_index is valid
  if (m->anim.branch_index < 0) m->anim.branch_index = 0;
  if (m->anim.branch_index >= NUM_BRANCHES) m->anim.branch_index = NUM_BRANCHES - 1;

  // Ensure direction is valid
  if (m->direction == 0) m->direction = 1;
  if (m->anim.vine_index <= 0) m->direction = 1;
  if (m->anim.vine_index >= NUM_VINES - 1) m->direction = -1;

  // After falling, always start with vine swing (safest)
  if (was_falling) {
    m->anim.current_trick = TRICK_VINE_SWING;
    m->anim.max_frames = VINE_SWING_FRAMES;
    return;
  }

  int roll = random_in_range(0, 99);

  if (roll < 40) {
    m->anim.current_trick = TRICK_VINE_SWING;
    m->anim.max_frames = VINE_SWING_FRAMES;

  } else if (roll < 50) {
    m->anim.current_trick = TRICK_CLIMB_VINE;
    m->anim.max_frames = CLIMB_FRAMES;
    m->anim.target_branch = random_in_range(0, 1);

  } else if (roll < 60) {
    m->anim.current_trick = TRICK_HANG_LOOK;
    m->anim.max_frames = HANG_LOOK_FRAMES;

  } else if (roll < 70) {
    m->anim.current_trick = TRICK_TAIL_HANG;
    m->anim.max_frames = TAIL_HANG_FRAMES;
    m->anim.branch_index = random_in_range(0, NUM_BRANCHES - 1);

  } else {
    // Higher chance of SIT_MUNCH, lower chance of FIGHT (simpler = more stable)
    m->anim.current_trick = TRICK_SIT_MUNCH;
    m->anim.max_frames = SIT_MUNCH_FRAMES;
    m->anim.branch_index = random_in_range(0, NUM_BRANCHES - 1);
  }
}

static void update_monkey(Monkey *m) {
  if (!m) return;

  m->anim.frame++;

  // Hard cap on frame to prevent overflow (reset if way over or wrapped negative)
  if (m->anim.frame < 0 || m->anim.frame > 500) {
    m->anim.frame = 0;
    m->anim.current_trick = TRICK_VINE_SWING;
    m->anim.max_frames = VINE_SWING_FRAMES;
  }

  // Validate max_frames to prevent infinite loops
  if (m->anim.max_frames <= 0 || m->anim.max_frames > 200) {
    m->anim.max_frames = VINE_SWING_FRAMES;
  }

  // Validate trick type (enum is unsigned, so only check upper bound)
  if (m->anim.current_trick >= TRICK_COUNT) {
    m->anim.current_trick = TRICK_VINE_SWING;
    m->anim.frame = 0;
    m->anim.max_frames = VINE_SWING_FRAMES;
  }

  switch (m->anim.current_trick) {
    case TRICK_VINE_SWING:  update_vine_swing(m); break;
    case TRICK_CLIMB_VINE:  update_climb_vine(m); break;
    case TRICK_HANG_LOOK:   update_hang_look(m); break;
    case TRICK_TAIL_HANG:   update_tail_hang(m); break;
    case TRICK_SIT_MUNCH:   update_sit_munch(m); break;
    case TRICK_FIGHT:       update_fight(m); break;
    case TRICK_FALLING:     update_falling(m); break;
    default:
      // Reset to safe state if invalid
      m->anim.current_trick = TRICK_VINE_SWING;
      m->anim.frame = 0;
      m->anim.max_frames = VINE_SWING_FRAMES;
      break;
  }

  // Animate tail and limbs
  m->tail_phase = (m->tail_phase + 120) & ANGLE_MASK;
  m->limb_phase = (m->limb_phase + 200) & ANGLE_MASK;

  if (m->anim.frame >= m->anim.max_frames) {
    select_next_trick(m);
  }

  // Bounds clamp
  if (m->pos.x < 10) m->pos.x = 10;
  if (m->pos.x > SCREEN_WIDTH - 10) m->pos.x = SCREEN_WIDTH - 10;
  if (m->pos.y < CANOPY_TOP + 15) m->pos.y = CANOPY_TOP + 15;
  if (m->pos.y > GROUND_Y - 5) m->pos.y = GROUND_Y - 5;
}

static void update_vines(void) {
  int delta = 50;
#ifndef PBL_COLOR
  delta = 30;
#endif
  if (s_low_power_mode || s_battery_level <= LOW_BATTERY_THRESHOLD) {
    delta = 20;
  }
  for (int i = 0; i < NUM_VINES; i++) {
    s_vines[i].sway_phase = (s_vines[i].sway_phase + delta) & ANGLE_MASK;
  }
}

// ============================================================================
// DRAWING
// ============================================================================

static void draw_canopy(GContext *ctx) {
  graphics_context_set_fill_color(ctx, COLOR_CANOPY_DARK);
  graphics_fill_rect(ctx, GRect(0, CANOPY_TOP - 10, SCREEN_WIDTH, 35), 0, GCornerNone);

  int step1 = 30;
  int step2 = 40;
#ifndef PBL_COLOR
  step1 = 36; step2 = 48;
#endif
  if (s_low_power_mode || s_battery_level <= LOW_BATTERY_THRESHOLD) { step1 += 12; step2 += 12; }

  for (int x = 0; x < SCREEN_WIDTH; x += step1) {
    graphics_fill_circle(ctx, GPoint(x, CANOPY_TOP + 5), 18);
  }

  graphics_context_set_fill_color(ctx, COLOR_CANOPY_LIGHT);
  for (int x = 15; x < SCREEN_WIDTH; x += step2) {
    graphics_fill_circle(ctx, GPoint(x, CANOPY_TOP - 3), 10);
  }
}

static void draw_branches(GContext *ctx) {
  for (int i = 0; i < NUM_BRANCHES; i++) {
    Branch *b = &s_branches[i];

    graphics_context_set_stroke_color(ctx, COLOR_BRANCH_DARK);
    graphics_context_set_stroke_width(ctx, b->thickness + 2);
    graphics_draw_line(ctx, GPoint(b->start.x, b->start.y + 2),
                            GPoint(b->end.x, b->end.y + 2));

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

    int segments = 4;
#ifndef PBL_COLOR
    segments = 3;
#endif
    if (s_low_power_mode || s_battery_level <= LOW_BATTERY_THRESHOLD) {
      segments = 3;
    }
    int seg_len = vine->length / segments;

    for (int j = 0; j < segments; j++) {
      int32_t angle = (vine->sway_phase + j * 1000) & ANGLE_MASK;
      int16_t sway = (sin_safe(angle) * vine->sway_amount) / TRIG_MAX_RATIO;

      next.x = current.x + sway;
      next.y = current.y + seg_len;

      graphics_draw_line(ctx, current, next);
      current = next;
    }

    graphics_context_set_fill_color(ctx, COLOR_CANOPY_LIGHT);
    int leaf_y = vine->top.y + vine->length / 2;
    graphics_fill_circle(ctx, GPoint(vine->top.x, leaf_y), 3);
  }
}

static void draw_ground(GContext *ctx) {
  graphics_context_set_fill_color(ctx, COLOR_GROUND);
  graphics_fill_rect(ctx, GRect(0, GROUND_Y, SCREEN_WIDTH, SCREEN_HEIGHT - GROUND_Y), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, COLOR_GROUND_DARK);
  graphics_fill_rect(ctx, GRect(0, GROUND_Y, SCREEN_WIDTH, 4), 0, GCornerNone);

  graphics_context_set_stroke_color(ctx, COLOR_CANOPY_LIGHT);
  graphics_context_set_stroke_width(ctx, 1);
  int tufts = 14;
#ifndef PBL_COLOR
  tufts = 10;
#endif
  if (s_low_power_mode || s_battery_level <= LOW_BATTERY_THRESHOLD) tufts -= 4;
  if (tufts < 6) tufts = 6;
  for (int i = 0; i < tufts; i++) {
    int x = 5 + (i * SCREEN_WIDTH / tufts);
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

  for (int i = 0; i < 3; i++) {
    int32_t angle = (m->tail_phase + i * 1500) & ANGLE_MASK;
    int16_t curl = (sin_safe(angle) * 3) / TRIG_MAX_RATIO;

    next.x = current.x - m->direction * 3 + curl;
    next.y = current.y + 3;

    graphics_draw_line(ctx, current, next);
    current = next;
  }

  graphics_fill_circle(ctx, current, 1);
}

static void draw_monkey(GContext *ctx, Monkey *m) {
  if (!ctx || !m) return;

  int16_t x = m->pos.x;
  int16_t y = m->pos.y;
  int dir = m->direction;

  // Ensure direction is valid
  if (dir == 0) dir = 1;

  bool hanging_from_vine = (m->anim.current_trick == TRICK_VINE_SWING ||
                            m->anim.current_trick == TRICK_CLIMB_VINE ||
                            m->anim.current_trick == TRICK_HANG_LOOK);
  bool hanging_upside_down = (m->anim.current_trick == TRICK_TAIL_HANG);
  bool sitting = (m->anim.current_trick == TRICK_SIT_MUNCH);
  bool fighting = (m->anim.current_trick == TRICK_FIGHT);
  bool falling = (m->anim.current_trick == TRICK_FALLING);

  bool in_air = false;
  if (m->anim.current_trick == TRICK_VINE_SWING) {
    int progress = clampi((m->anim.frame * 100) / VINE_SWING_FRAMES, 0, 100);
    if (progress >= 35 && progress < 65) {
      in_air = true;
      hanging_from_vine = false;
    }
  }

  GPoint grip_point = {x, y - 15};
  if (hanging_from_vine && m->anim.vine_index >= 0 && m->anim.vine_index < NUM_VINES) {
    grip_point.x = x;
    grip_point.y = y - 18;
  } else if (hanging_upside_down && m->anim.branch_index >= 0 && m->anim.branch_index < NUM_BRANCHES) {
    Branch *branch = &s_branches[m->anim.branch_index];
    grip_point.x = x;
    grip_point.y = (branch->start.y + branch->end.y) / 2;
  }

  if (!hanging_upside_down) {
    draw_monkey_tail(ctx, m, x, y);
  }

  graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
  graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
  graphics_context_set_stroke_width(ctx, 3);

  if (hanging_upside_down) {
    graphics_fill_rect(ctx, GRect(x - 5, y - 6, 10, 12), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
    graphics_fill_rect(ctx, GRect(x - 3, y - 4, 6, 8), 2, GCornersAll);

    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_draw_line(ctx, GPoint(x - 3, y - 6), GPoint(grip_point.x - 3, grip_point.y));
    graphics_draw_line(ctx, GPoint(x + 3, y - 6), GPoint(grip_point.x + 3, grip_point.y));
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_circle(ctx, GPoint(grip_point.x - 3, grip_point.y), 2);
    graphics_fill_circle(ctx, GPoint(grip_point.x + 3, grip_point.y), 2);

    int arm_dangle = (sin_safe(m->limb_phase) * 3) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(x - 5, y + 4), GPoint(x - 7 + arm_dangle, y + 12));
    graphics_draw_line(ctx, GPoint(x + 5, y + 4), GPoint(x + 7 - arm_dangle, y + 12));
    graphics_fill_circle(ctx, GPoint(x - 7 + arm_dangle, y + 12), 2);
    graphics_fill_circle(ctx, GPoint(x + 7 - arm_dangle, y + 12), 2);

  } else if (hanging_from_vine) {
    graphics_context_set_stroke_color(ctx, COLOR_VINE);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(x, y - 16), GPoint(x, CANOPY_TOP + 10));

    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_rect(ctx, GRect(x - 5, y - 5, 10, 12), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
    graphics_fill_rect(ctx, GRect(x - 3, y - 2, 6, 8), 2, GCornersAll);

    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(x - 4, y - 5), GPoint(x, y - 16));
    graphics_draw_line(ctx, GPoint(x + 4, y - 5), GPoint(x, y - 16));
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_circle(ctx, GPoint(x, y - 16), 3);

    // safer leg swing
    int leg_offset = (sin_safe(m->anim.rotation) * 6) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - 5 - leg_offset, y + 15));
    graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + 5 - leg_offset, y + 15));
    graphics_fill_circle(ctx, GPoint(x - 5 - leg_offset, y + 15), 2);
    graphics_fill_circle(ctx, GPoint(x + 5 - leg_offset, y + 15), 2);

  } else if (sitting) {
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_rect(ctx, GRect(x - 5, y - 3, 10, 10), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
    graphics_fill_rect(ctx, GRect(x - 3, y - 1, 6, 7), 2, GCornersAll);

    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(x - 4, y + 7), GPoint(x - 6, y + 5));
    graphics_draw_line(ctx, GPoint(x + 4, y + 7), GPoint(x + 6, y + 5));
    graphics_fill_circle(ctx, GPoint(x - 6, y + 5), 2);
    graphics_fill_circle(ctx, GPoint(x + 6, y + 5), 2);

    int munch_phase = (sin_safe(m->limb_phase) * 4) / TRIG_MAX_RATIO;
    int apple_x = x + dir * 6;
    int apple_y = y - 8 + munch_phase;

    graphics_draw_line(ctx, GPoint(x - dir * 5, y), GPoint(x - dir * 8, y + 5));
    graphics_fill_circle(ctx, GPoint(x - dir * 8, y + 5), 2);

    graphics_draw_line(ctx, GPoint(x + dir * 5, y - 2), GPoint(apple_x, apple_y + 3));
    graphics_fill_circle(ctx, GPoint(apple_x, apple_y + 3), 2);

    int bites = m->anim.target_branch;
    int apple_radius = clampi(5 - bites, 0, 5);
    if (apple_radius > 1) {
      graphics_context_set_fill_color(ctx, COLOR_APPLE);
      graphics_fill_circle(ctx, GPoint(apple_x, apple_y), apple_radius);

      if (bites > 0) {
        graphics_context_set_fill_color(ctx, COLOR_APPLE_BITE);
        graphics_fill_circle(ctx, GPoint(apple_x - dir * 2, apple_y), bites);
      }

      graphics_context_set_stroke_color(ctx, COLOR_BRANCH);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, GPoint(apple_x, apple_y - apple_radius),
                              GPoint(apple_x + 1, apple_y - apple_radius - 2));
    }

  } else if (fighting) {
    int fight_progress = clampi((m->anim.frame * 100) / FIGHT_FRAMES, 0, 100);
    bool tussling = (fight_progress >= 30 && fight_progress < 80);

    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_rect(ctx, GRect(x - 5, y - 5, 10, 12), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
    graphics_fill_rect(ctx, GRect(x - 3, y - 2, 6, 8), 2, GCornersAll);

    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 3);

    if (tussling) {
      int arm_swing = (sin_safe(m->limb_phase * 3) * 10) / TRIG_MAX_RATIO;
      graphics_draw_line(ctx, GPoint(x - 5, y - 2), GPoint(x - 12 + arm_swing, y - 8));
      graphics_draw_line(ctx, GPoint(x + 5, y - 2), GPoint(x + 12 - arm_swing, y - 8));
      graphics_fill_circle(ctx, GPoint(x - 12 + arm_swing, y - 8), 2);
      graphics_fill_circle(ctx, GPoint(x + 12 - arm_swing, y - 8), 2);
    } else {
      graphics_draw_line(ctx, GPoint(x - 5, y - 2), GPoint(x - 10, y - 6));
      graphics_draw_line(ctx, GPoint(x + 5, y - 2), GPoint(x + 10, y - 6));
      graphics_fill_circle(ctx, GPoint(x - 10, y - 6), 2);
      graphics_fill_circle(ctx, GPoint(x + 10, y - 6), 2);
    }

    graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - 7, y + 14));
    graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + 7, y + 14));
    graphics_fill_circle(ctx, GPoint(x - 7, y + 14), 2);
    graphics_fill_circle(ctx, GPoint(x + 7, y + 14), 2);

  } else if (falling) {
    int fall_progress = clampi((m->anim.frame * 100) / FALLING_FRAMES, 0, 100);

    int rot_offset_x = (sin_safe(m->anim.rotation) * 3) / TRIG_MAX_RATIO;
    int rot_offset_y = (cos_safe(m->anim.rotation) * 2) / TRIG_MAX_RATIO;

    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_rect(ctx, GRect(x - 5 + rot_offset_x, y - 5 + rot_offset_y, 10, 12), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
    graphics_fill_rect(ctx, GRect(x - 3 + rot_offset_x, y - 2 + rot_offset_y, 6, 8), 2, GCornersAll);

    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 3);

    int flail = (sin_safe(m->limb_phase) * 12) / TRIG_MAX_RATIO;
    int flail2 = (cos_safe(m->limb_phase) * 10) / TRIG_MAX_RATIO;

    graphics_draw_line(ctx, GPoint(x - 5, y - 2), GPoint(x - 10 + flail, y - 8 + flail2));
    graphics_draw_line(ctx, GPoint(x + 5, y - 2), GPoint(x + 10 - flail, y - 6 - flail2));
    graphics_fill_circle(ctx, GPoint(x - 10 + flail, y - 8 + flail2), 2);
    graphics_fill_circle(ctx, GPoint(x + 10 - flail, y - 6 - flail2), 2);

    graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - 8 - flail2, y + 14 + flail));
    graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + 8 + flail2, y + 12 - flail));
    graphics_fill_circle(ctx, GPoint(x - 8 - flail2, y + 14 + flail), 2);
    graphics_fill_circle(ctx, GPoint(x + 8 + flail2, y + 12 - flail), 2);

    if (fall_progress >= 60) {
      graphics_context_set_fill_color(ctx, COLOR_STAR);
      int star_phase = fall_progress * 5;
      for (int i = 0; i < 3; i++) {
        int star_angle = (star_phase + i * TRIG_MAX_ANGLE / 3) & ANGLE_MASK;
        int star_x = x + (sin_safe(star_angle) * 12) / TRIG_MAX_RATIO;
        int star_y = y - 18 + (cos_safe(star_angle) * 5) / TRIG_MAX_RATIO;
        graphics_fill_circle(ctx, GPoint(star_x, star_y), 2);
      }
    }

  } else {
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
    graphics_fill_rect(ctx, GRect(x - 5, y - 5, 10, 12), 3, GCornersAll);
    graphics_context_set_fill_color(ctx, COLOR_MONKEY_BELLY);
    graphics_fill_rect(ctx, GRect(x - 3, y - 2, 6, 8), 2, GCornersAll);

    graphics_context_set_stroke_color(ctx, COLOR_MONKEY_FUR);
    graphics_context_set_stroke_width(ctx, 3);
    int spread = in_air ? 8 : 5;

    graphics_draw_line(ctx, GPoint(x - 5, y), GPoint(x - spread, y - 2));
    graphics_draw_line(ctx, GPoint(x + 5, y), GPoint(x + spread, y - 2));
    graphics_fill_circle(ctx, GPoint(x - spread, y - 2), 2);
    graphics_fill_circle(ctx, GPoint(x + spread, y - 2), 2);

    graphics_draw_line(ctx, GPoint(x - 3, y + 7), GPoint(x - spread + 2, y + 12));
    graphics_draw_line(ctx, GPoint(x + 3, y + 7), GPoint(x + spread - 2, y + 12));
    graphics_fill_circle(ctx, GPoint(x - spread + 2, y + 12), 2);
    graphics_fill_circle(ctx, GPoint(x + spread - 2, y + 12), 2);
  }

  // HEAD
  int head_y = hanging_upside_down ? y + 12 : y - 10;

  graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
  graphics_fill_circle(ctx, GPoint(x, head_y), 7);

  graphics_context_set_fill_color(ctx, COLOR_MONKEY_FACE);
  graphics_fill_circle(ctx, GPoint(x + dir * 2, head_y + (hanging_upside_down ? -1 : 1)), 5);

  graphics_context_set_fill_color(ctx, COLOR_MONKEY_FUR);
  graphics_fill_circle(ctx, GPoint(x - 6, head_y), 3);
  graphics_fill_circle(ctx, GPoint(x + 6, head_y), 3);

  graphics_context_set_fill_color(ctx, COLOR_MONKEY_FACE);
  graphics_fill_circle(ctx, GPoint(x - 6, head_y), 1);
  graphics_fill_circle(ctx, GPoint(x + 6, head_y), 1);

  graphics_context_set_fill_color(ctx, COLOR_MONKEY_DARK);
  int eye_y = head_y + (hanging_upside_down ? 2 : -2);
  graphics_fill_circle(ctx, GPoint(x + dir * 1, eye_y), 1);
  graphics_fill_circle(ctx, GPoint(x + dir * 4, eye_y), 1);

  graphics_context_set_stroke_color(ctx, COLOR_MONKEY_DARK);
  graphics_context_set_stroke_width(ctx, 1);
  int mouth_y = head_y + (hanging_upside_down ? -3 : 3);
  graphics_draw_line(ctx, GPoint(x + dir * 1, mouth_y), GPoint(x + dir * 4, mouth_y));

  if (hanging_upside_down) {
    draw_monkey_tail(ctx, m, x, y);
  }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  if (!layer || !ctx || !s_window_loaded || !s_fully_initialized) return;

  graphics_context_set_fill_color(ctx, COLOR_SKY);
  graphics_fill_rect(ctx, GRect(0, 0, SCREEN_WIDTH, CANOPY_TOP + 20), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, COLOR_SKY_LOW);
  graphics_fill_rect(ctx, GRect(0, CANOPY_TOP + 20, SCREEN_WIDTH, GROUND_Y - CANOPY_TOP - 20), 0, GCornerNone);

  draw_canopy(ctx);
  draw_branches(ctx);
  draw_vines(ctx);

  for (int i = 0; i < NUM_MONKEYS; i++) {
    if (i >= 0 && i < NUM_MONKEYS && s_monkeys[i].active) {
      draw_monkey(ctx, &s_monkeys[i]);
    }
  }

  draw_ground(ctx);

  // Battery indicator
  int batt_x = SCREEN_WIDTH - 28;
  int batt_y = 4;
  int batt_width = 22;
  int batt_height = 10;

  graphics_context_set_stroke_color(ctx, COLOR_TIME_TEXT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(batt_x, batt_y, batt_width, batt_height));

  graphics_context_set_fill_color(ctx, COLOR_TIME_TEXT);
  graphics_fill_rect(ctx, GRect(batt_x + batt_width, batt_y + 3, 2, 4), 0, GCornerNone);

  int fill_width = (s_battery_level * (batt_width - 4)) / 100;
  if (fill_width < 2) fill_width = 2;

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
// TIMER
// ============================================================================

static void animation_timer_callback(void *data) {
  // Clear timer handle first (it's no longer valid after firing)
  s_animation_timer = NULL;

  // If not running or window not present, stop here and don't reschedule
  if (!should_animate()) {
    return;
  }

  // Update animation state with safety checks
  update_vines();
  for (int i = 0; i < NUM_MONKEYS; i++) {
    if (i >= 0 && i < NUM_MONKEYS && s_monkeys[i].active) {
      update_monkey(&s_monkeys[i]);
    }
  }

  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);

  // Schedule next frame last, only if still running
  if (should_animate() && !s_animation_timer) {
    s_animation_timer = app_timer_register(get_animation_interval(), animation_timer_callback, NULL);
  }
}

// ============================================================================
// TIME + SERVICES
// ============================================================================

static void update_time(void) {
  // Guard against calls when not ready
  if (!s_fully_initialized || !s_window_loaded || !s_time_layer || !s_date_layer) return;

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (!s_running || !s_fully_initialized) return;
  update_time();
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  s_is_charging = state.is_plugged;
  if (s_fully_initialized) {
    ensure_timer_running();
    if (s_canvas_layer && s_window_loaded) layer_mark_dirty(s_canvas_layer);
  }
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  if (!s_running || !s_fully_initialized || !s_in_focus) return;
  if (!data || num_samples == 0) return;

  // Check cooldown
  uint32_t now = time(NULL) * 1000;  // rough ms (second precision is enough)
  if (now - s_last_shake_time < SHAKE_COOLDOWN_MS) return;

  // Check for vigorous shake - look for high magnitude in any sample
  bool vigorous = false;
  for (uint32_t i = 0; i < num_samples && !vigorous; i++) {
    int32_t x = data[i].x;
    int32_t y = data[i].y;
    int32_t z = data[i].z;
    int32_t mag_sq = x * x + y * y + z * z;
    if (mag_sq > SHAKE_THRESHOLD_SQ) {
      vigorous = true;
    }
  }

  if (!vigorous) return;

  // Vigorous shake detected - trigger falls
  s_last_shake_time = now;
  bool any_fell = false;
  for (int i = 0; i < NUM_MONKEYS; i++) {
    if (s_monkeys[i].active && s_monkeys[i].anim.current_trick != TRICK_FALLING) {
      trigger_fall(&s_monkeys[i]);
      any_fell = true;
    }
  }
  if (any_fell && s_vibes_enabled) vibes_short_pulse();
  if (s_canvas_layer && s_window_loaded) layer_mark_dirty(s_canvas_layer);
}

// Connection change handler (pause when disconnected)
static void bt_handler(bool connected) {
  s_bt_connected = connected;
  if (s_fully_initialized) {
    ensure_timer_running();
  }
}

// User button toggles for low power and vibrations
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (!s_fully_initialized) return;
  s_low_power_mode = !s_low_power_mode;
  persist_write_bool(PERSIST_KEY_LOW_POWER, s_low_power_mode);
  ensure_timer_running();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (!s_fully_initialized) return;
  s_vibes_enabled = !s_vibes_enabled;
  persist_write_bool(PERSIST_KEY_VIBES, s_vibes_enabled);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

// ============================================================================
// WINDOW
// ============================================================================

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  srand(time(NULL));

  s_window_loaded = true;
  s_running = true;

  init_vines();
  init_branches();
  init_monkeys();

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
  window_set_click_config_provider(window, click_config_provider);

  s_time_layer = text_layer_create(GRect(0, TIME_Y, bounds.size.w, 38));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, COLOR_TIME_TEXT);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  s_date_layer = text_layer_create(GRect(0, DATE_Y, bounds.size.w, 20));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, COLOR_TIME_TEXT);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  battery_callback(battery_state_service_peek());

  // Mark as fully initialized before updating time display
  s_fully_initialized = true;

  // Now safe to update time (guard checks s_fully_initialized)
  update_time();

  // Start/stop animation loop based on current conditions
  ensure_timer_running();
}

static void main_window_unload(Window *window) {
  // Mark as not ready FIRST to stop all callbacks
  s_fully_initialized = false;
  s_window_loaded = false;
  s_running = false;

  // Cancel timer before destroying anything
  if (s_animation_timer) {
    app_timer_cancel(s_animation_timer);
    s_animation_timer = NULL;
  }

  // ✅ destroy + NULL everything (prevents use-after-free crashes)
  if (s_time_layer) {
    text_layer_destroy(s_time_layer);
    s_time_layer = NULL;
  }
  if (s_date_layer) {
    text_layer_destroy(s_date_layer);
    s_date_layer = NULL;
  }
  if (s_canvas_layer) {
    layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
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

  // Use raw accel data for vigorous shake detection (25Hz, 5 samples per batch)
  accel_data_service_subscribe(5, accel_data_handler);

  // Pause animation when app loses focus (e.g., notifications)
  app_focus_service_subscribe(focus_handler);

  // Pause animation when not connected to phone (saves power)
  bluetooth_connection_service_subscribe(bt_handler);

  // Load persisted settings
  if (persist_exists(PERSIST_KEY_LOW_POWER)) {
    s_low_power_mode = persist_read_bool(PERSIST_KEY_LOW_POWER);
  }
  if (persist_exists(PERSIST_KEY_VIBES)) {
    s_vibes_enabled = persist_read_bool(PERSIST_KEY_VIBES);
  }

  s_bt_connected = bluetooth_connection_service_peek();
}

static void deinit(void) {
  s_running = false;
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_data_service_unsubscribe();
  app_focus_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();

  if (s_main_window) {
    window_destroy(s_main_window);
    s_main_window = NULL;
  }
}

// Focus handler keeps animation paused when not visible
static void focus_handler(bool in_focus) {
  s_in_focus = in_focus;
  s_running = in_focus && s_window_loaded && s_fully_initialized;

  // When losing focus, immediately stop timer
  if (!in_focus) {
    if (s_animation_timer) {
      app_timer_cancel(s_animation_timer);
      s_animation_timer = NULL;
    }
  } else {
    // When gaining focus, restart timer if conditions are met
    ensure_timer_running();
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
