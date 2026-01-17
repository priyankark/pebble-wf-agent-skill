/**
 * Pocket Garden - Interactive Plant Growing Watchface
 *
 * Grow a virtual plant by watering it regularly throughout the day!
 * Press SELECT to water. Neglect it and it wilts.
 */

#include <pebble.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define ANIMATION_INTERVAL 50
#define ANIMATION_INTERVAL_LOW_POWER 100
#define LOW_BATTERY_THRESHOLD 20

// Game mechanics
#define WATER_MAX 100
#define WATER_PER_PRESS 30
#define WATER_DECAY_INTERVAL 1800   // 30 minutes in seconds
#define WATER_DECAY_AMOUNT 12

// Growth thresholds
#define WATER_THRIVING_MIN 70
#define WATER_HEALTHY_MIN 40
#define WATER_THIRSTY_MIN 20

#define GROWTH_PER_WATERING 8
#define GROWTH_TO_NEXT_STAGE 100

// Water drops for splash effect
#define MAX_WATER_DROPS 5

// Persistent storage keys
#define STORAGE_KEY_PLANT 1

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef enum {
    STAGE_SEED = 0,
    STAGE_SPROUT = 1,
    STAGE_SMALL = 2,
    STAGE_FULL = 3,
    STAGE_FLOWERING = 4
} GrowthStage;

typedef enum {
    HEALTH_THRIVING = 0,
    HEALTH_HEALTHY = 1,
    HEALTH_THIRSTY = 2,
    HEALTH_WILTING = 3
} HealthState;

typedef struct {
    GrowthStage stage;
    uint8_t water_level;
    uint8_t growth_progress;
    time_t last_watered;
    uint16_t total_waters;
} PlantState;

typedef struct {
    GPoint pos;
    int16_t vel_x;
    int16_t vel_y;
    int size;
    bool active;
} WaterDrop;

// ============================================================================
// GLOBAL STATE
// ============================================================================

static Window *s_main_window;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static AppTimer *s_animation_timer;

static PlantState s_plant;
static WaterDrop s_drops[MAX_WATER_DROPS];

static int s_battery_level = 100;
static int32_t s_sway_phase = 0;
static int32_t s_leaf_phase = 0;
static int s_wilt_offset = 0;
static bool s_is_watering = false;
static int s_water_frame = 0;
static int s_growth_anim = 0;

static int16_t s_screen_width;
static int16_t s_screen_height;
static int16_t s_center_x;

// ============================================================================
// COLOR DEFINITIONS
// ============================================================================

#ifdef PBL_COLOR
    #define COLOR_SKY GColorPictonBlue
    #define COLOR_POT GColorBulgarianRose
    #define COLOR_POT_DARK GColorDarkCandyAppleRed
    #define COLOR_POT_RIM GColorMelon
    #define COLOR_SOIL GColorWindsorTan
    #define COLOR_STEM GColorIslamicGreen
    #define COLOR_LEAF GColorGreen
    #define COLOR_LEAF_LIGHT GColorMayGreen
    #define COLOR_STEM_WILT GColorLimerick
    #define COLOR_LEAF_WILT GColorChromeYellow
    #define COLOR_FLOWER_1 GColorRed
    #define COLOR_FLOWER_2 GColorMagenta
    #define COLOR_FLOWER_3 GColorOrange
    #define COLOR_FLOWER_CENTER GColorYellow
    #define COLOR_WATER GColorCyan
    #define COLOR_SEED GColorWindsorTan
    #define COLOR_TEXT GColorWhite
#else
    #define COLOR_SKY GColorWhite
    #define COLOR_POT GColorDarkGray
    #define COLOR_POT_DARK GColorBlack
    #define COLOR_POT_RIM GColorLightGray
    #define COLOR_SOIL GColorBlack
    #define COLOR_STEM GColorBlack
    #define COLOR_LEAF GColorBlack
    #define COLOR_LEAF_LIGHT GColorDarkGray
    #define COLOR_STEM_WILT GColorLightGray
    #define COLOR_LEAF_WILT GColorLightGray
    #define COLOR_FLOWER_1 GColorWhite
    #define COLOR_FLOWER_2 GColorWhite
    #define COLOR_FLOWER_3 GColorWhite
    #define COLOR_FLOWER_CENTER GColorBlack
    #define COLOR_WATER GColorLightGray
    #define COLOR_SEED GColorDarkGray
    #define COLOR_TEXT GColorBlack
#endif

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static int random_in_range(int min, int max) {
    if (max <= min) return min;
    return min + (rand() % (max - min + 1));
}

static HealthState get_health_state(void) {
    if (s_plant.water_level >= WATER_THRIVING_MIN) return HEALTH_THRIVING;
    if (s_plant.water_level >= WATER_HEALTHY_MIN) return HEALTH_HEALTHY;
    if (s_plant.water_level >= WATER_THIRSTY_MIN) return HEALTH_THIRSTY;
    return HEALTH_WILTING;
}

// ============================================================================
// PERSISTENCE
// ============================================================================

static void save_plant_state(void) {
    persist_write_data(STORAGE_KEY_PLANT, &s_plant, sizeof(PlantState));
}

static void load_plant_state(void) {
    if (persist_exists(STORAGE_KEY_PLANT)) {
        persist_read_data(STORAGE_KEY_PLANT, &s_plant, sizeof(PlantState));

        // Validate
        if (s_plant.stage > STAGE_FLOWERING) s_plant.stage = STAGE_SEED;
        if (s_plant.water_level > WATER_MAX) s_plant.water_level = WATER_MAX;

        // Apply decay since last run
        time_t now = time(NULL);
        if (s_plant.last_watered > 0) {
            int32_t elapsed = now - s_plant.last_watered;
            int intervals = elapsed / WATER_DECAY_INTERVAL;
            int decay = intervals * WATER_DECAY_AMOUNT;
            if (decay >= s_plant.water_level) {
                s_plant.water_level = 0;
            } else {
                s_plant.water_level -= decay;
            }
        }
    } else {
        // New plant
        s_plant.stage = STAGE_SEED;
        s_plant.water_level = 50;
        s_plant.growth_progress = 0;
        s_plant.last_watered = time(NULL);
        s_plant.total_waters = 0;
    }
}

// ============================================================================
// GAME LOGIC
// ============================================================================

static void reset_plant_to_seed(void) {
    // Phoenix rebirth - plant dies and is reborn as seed
    s_plant.stage = STAGE_SEED;
    s_plant.water_level = 30;  // Start with some water
    s_plant.growth_progress = 0;
    s_plant.last_watered = time(NULL);
    // Keep total_waters as a memorial to past lives

    // Trigger haptic feedback for death/rebirth
    vibes_long_pulse();

    save_plant_state();
}

static void check_plant_death(void) {
    // Plant dies if water has been at 0 for too long (completely dried out)
    // Or if it's been wilting (< WATER_THIRSTY_MIN) for extended period
    if (s_plant.water_level == 0 && s_plant.stage > STAGE_SEED) {
        // Plant has completely dried out - time to be reborn
        reset_plant_to_seed();
    }
}

static void start_water_splash(void) {
    int plant_top_y = s_screen_height - 60 - (s_plant.stage * 12);

    for (int i = 0; i < MAX_WATER_DROPS; i++) {
        s_drops[i].pos.x = s_center_x + random_in_range(-20, 20);
        s_drops[i].pos.y = plant_top_y;
        s_drops[i].vel_x = random_in_range(-2, 2);
        s_drops[i].vel_y = random_in_range(-4, -1);
        s_drops[i].size = random_in_range(2, 4);
        s_drops[i].active = true;
    }
}

static void water_plant(void) {
    // Add water
    int new_water = s_plant.water_level + WATER_PER_PRESS;
    s_plant.water_level = (new_water > WATER_MAX) ? WATER_MAX : new_water;
    s_plant.last_watered = time(NULL);
    s_plant.total_waters++;

    // Add growth if not wilting and not at max stage
    if (get_health_state() <= HEALTH_HEALTHY && s_plant.stage < STAGE_FLOWERING) {
        s_plant.growth_progress += GROWTH_PER_WATERING;

        if (s_plant.growth_progress >= GROWTH_TO_NEXT_STAGE) {
            s_plant.growth_progress = 0;
            s_plant.stage++;
            s_growth_anim = 20;
        }
    }

    // Start animation
    s_is_watering = true;
    s_water_frame = 0;
    start_water_splash();

    // Haptic feedback
    vibes_short_pulse();

    save_plant_state();
}

// ============================================================================
// DRAWING FUNCTIONS
// ============================================================================

static void draw_pot(GContext *ctx, int16_t y_base) {
    int pot_width = 60;
    int pot_height = 25;
    int rim_height = 4;
    int pot_top = y_base - pot_height;
    int pot_left = s_center_x - pot_width / 2;

    // Pot body (trapezoid - wider at top)
    graphics_context_set_fill_color(ctx, COLOR_POT);
    GPoint pot_points[4] = {
        {pot_left, pot_top + rim_height},
        {pot_left + pot_width, pot_top + rim_height},
        {pot_left + pot_width - 8, y_base},
        {pot_left + 8, y_base}
    };
    GPathInfo pot_info = { .num_points = 4, .points = pot_points };
    GPath *pot_path = gpath_create(&pot_info);
    gpath_draw_filled(ctx, pot_path);
    gpath_destroy(pot_path);

    // Pot rim
    graphics_context_set_fill_color(ctx, COLOR_POT_RIM);
    GRect rim = GRect(pot_left - 2, pot_top, pot_width + 4, rim_height);
    graphics_fill_rect(ctx, rim, 2, GCornersTop);

    // Soil surface
    graphics_context_set_fill_color(ctx, COLOR_SOIL);
    GRect soil = GRect(pot_left + 2, pot_top + rim_height, pot_width - 4, 8);
    graphics_fill_rect(ctx, soil, 0, GCornerNone);
}

static void draw_seed(GContext *ctx, int16_t cx, int16_t y_base) {
    // Small seed poking out of soil
    int16_t seed_y = y_base - 30;

    graphics_context_set_fill_color(ctx, COLOR_SEED);
    graphics_fill_circle(ctx, GPoint(cx, seed_y + 4), 5);
    graphics_fill_circle(ctx, GPoint(cx, seed_y), 3);
}

static void draw_leaf(GContext *ctx, int16_t x, int16_t y, int size, int angle_deg, bool is_wilting) {
    GColor leaf_color = is_wilting ? COLOR_LEAF_WILT : COLOR_LEAF;
    graphics_context_set_fill_color(ctx, leaf_color);

    // Simple leaf as ellipse-like shape
    int w = size;
    int h = size / 2 + 1;

    // Adjust for angle (simplified)
    int dx = 0;
    if (angle_deg < 0) dx = -w/2;
    else if (angle_deg > 0) dx = w/2;

    graphics_fill_circle(ctx, GPoint(x + dx, y), h);
    graphics_fill_circle(ctx, GPoint(x + dx + (angle_deg > 0 ? 2 : -2), y), h - 1);
}

static void draw_sprout(GContext *ctx, int16_t cx, int16_t y_base, int16_t sway) {
    bool wilting = get_health_state() >= HEALTH_THIRSTY;
    int wilt_droop = wilting ? s_wilt_offset : 0;

    int stem_height = 20;
    int stem_top_x = cx + sway + wilt_droop;
    int stem_top_y = y_base - 28 - stem_height;

    // Stem
    graphics_context_set_stroke_color(ctx, wilting ? COLOR_STEM_WILT : COLOR_STEM);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(cx, y_base - 28), GPoint(stem_top_x, stem_top_y));

    // Two small leaves
    draw_leaf(ctx, stem_top_x - 4, stem_top_y + 3, 8, -45, wilting);
    draw_leaf(ctx, stem_top_x + 4, stem_top_y + 3, 8, 45, wilting);
}

static void draw_small_plant(GContext *ctx, int16_t cx, int16_t y_base, int16_t sway) {
    bool wilting = get_health_state() >= HEALTH_THIRSTY;
    int wilt_droop = wilting ? s_wilt_offset : 0;

    int stem_height = 35;
    int stem_top_x = cx + sway + wilt_droop;
    int stem_top_y = y_base - 28 - stem_height;

    // Main stem with curve
    graphics_context_set_stroke_color(ctx, wilting ? COLOR_STEM_WILT : COLOR_STEM);
    graphics_context_set_stroke_width(ctx, 3);

    // Draw stem in segments for curve
    int seg_h = stem_height / 3;
    int prev_x = cx;
    int prev_y = y_base - 28;
    for (int i = 1; i <= 3; i++) {
        int seg_sway = (sway + wilt_droop) * i / 3;
        int new_x = cx + seg_sway;
        int new_y = y_base - 28 - seg_h * i;
        graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(new_x, new_y));
        prev_x = new_x;
        prev_y = new_y;
    }

    // Leaves at different heights
    int leaf_flutter = (sin_lookup(s_leaf_phase) * 2) / TRIG_MAX_RATIO;

    draw_leaf(ctx, cx + sway/3 - 10, y_base - 40, 12, -60, wilting);
    draw_leaf(ctx, cx + sway/3 + 10, y_base - 45, 12, 60, wilting);
    draw_leaf(ctx, stem_top_x - 8 + leaf_flutter, stem_top_y + 5, 14, -45, wilting);
    draw_leaf(ctx, stem_top_x + 8 - leaf_flutter, stem_top_y + 5, 14, 45, wilting);
}

static void draw_flower(GContext *ctx, int16_t x, int16_t y, int size, GColor petal_color) {
    // 5 petals around center
    graphics_context_set_fill_color(ctx, petal_color);

    int petal_dist = size / 2 + 2;
    for (int i = 0; i < 5; i++) {
        int32_t angle = (TRIG_MAX_ANGLE * i) / 5;
        int px = x + (cos_lookup(angle) * petal_dist) / TRIG_MAX_RATIO;
        int py = y + (sin_lookup(angle) * petal_dist) / TRIG_MAX_RATIO;
        graphics_fill_circle(ctx, GPoint(px, py), size / 2);
    }

    // Center
    graphics_context_set_fill_color(ctx, COLOR_FLOWER_CENTER);
    graphics_fill_circle(ctx, GPoint(x, y), size / 3 + 1);
}

static void draw_full_plant(GContext *ctx, int16_t cx, int16_t y_base, int16_t sway) {
    bool wilting = get_health_state() >= HEALTH_THIRSTY;
    int wilt_droop = wilting ? s_wilt_offset : 0;

    int stem_height = 50;
    int stem_top_x = cx + sway + wilt_droop;
    int stem_top_y = y_base - 28 - stem_height;

    // Main stem
    graphics_context_set_stroke_color(ctx, wilting ? COLOR_STEM_WILT : COLOR_STEM);
    graphics_context_set_stroke_width(ctx, 4);

    int seg_h = stem_height / 4;
    int prev_x = cx;
    int prev_y = y_base - 28;
    for (int i = 1; i <= 4; i++) {
        int seg_sway = (sway + wilt_droop) * i / 4;
        int new_x = cx + seg_sway;
        int new_y = y_base - 28 - seg_h * i;
        graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(new_x, new_y));
        prev_x = new_x;
        prev_y = new_y;
    }

    // Many leaves
    int leaf_flutter = (sin_lookup(s_leaf_phase) * 3) / TRIG_MAX_RATIO;

    // Lower leaves
    draw_leaf(ctx, cx - 12, y_base - 38, 14, -70, wilting);
    draw_leaf(ctx, cx + 12, y_base - 42, 14, 70, wilting);

    // Middle leaves
    int mid_x = cx + sway/2;
    draw_leaf(ctx, mid_x - 14 + leaf_flutter, y_base - 55, 16, -55, wilting);
    draw_leaf(ctx, mid_x + 14 - leaf_flutter, y_base - 58, 16, 55, wilting);

    // Top leaves
    draw_leaf(ctx, stem_top_x - 10 + leaf_flutter, stem_top_y + 8, 15, -40, wilting);
    draw_leaf(ctx, stem_top_x + 10 - leaf_flutter, stem_top_y + 8, 15, 40, wilting);
    draw_leaf(ctx, stem_top_x, stem_top_y + 2, 12, 0, wilting);
}

static void draw_flowering_plant(GContext *ctx, int16_t cx, int16_t y_base, int16_t sway) {
    // Draw base plant first
    draw_full_plant(ctx, cx, y_base, sway);

    bool wilting = get_health_state() >= HEALTH_THIRSTY;
    int wilt_droop = wilting ? s_wilt_offset : 0;

    int stem_height = 50;
    int stem_top_x = cx + sway + wilt_droop;
    int stem_top_y = y_base - 28 - stem_height;

    // Draw flowers at top
    if (!wilting) {
        // Main flower
        draw_flower(ctx, stem_top_x, stem_top_y - 8, 12, COLOR_FLOWER_1);

        // Side flowers
        draw_flower(ctx, stem_top_x - 18, stem_top_y + 10, 10, COLOR_FLOWER_2);
        draw_flower(ctx, stem_top_x + 16, stem_top_y + 6, 10, COLOR_FLOWER_3);
    } else {
        // Drooping flower buds when wilting
        graphics_context_set_fill_color(ctx, COLOR_LEAF_WILT);
        graphics_fill_circle(ctx, GPoint(stem_top_x + 5, stem_top_y - 3), 5);
        graphics_fill_circle(ctx, GPoint(stem_top_x - 15, stem_top_y + 12), 4);
    }
}

static void draw_plant(GContext *ctx, int16_t y_base) {
    // Calculate sway
    int sway_amp = 2 + s_plant.stage;
    if (get_health_state() >= HEALTH_THIRSTY) sway_amp /= 2;
    int16_t sway = (sin_lookup(s_sway_phase) * sway_amp) / TRIG_MAX_RATIO;

    // Apply growth animation scale
    if (s_growth_anim > 0) {
        // Bounce effect
        int scale = 100 + (10 - abs(s_growth_anim - 10));
        sway = (sway * scale) / 100;
    }

    switch (s_plant.stage) {
        case STAGE_SEED:
            draw_seed(ctx, s_center_x, y_base);
            break;
        case STAGE_SPROUT:
            draw_sprout(ctx, s_center_x, y_base, sway);
            break;
        case STAGE_SMALL:
            draw_small_plant(ctx, s_center_x, y_base, sway);
            break;
        case STAGE_FULL:
            draw_full_plant(ctx, s_center_x, y_base, sway);
            break;
        case STAGE_FLOWERING:
            draw_flowering_plant(ctx, s_center_x, y_base, sway);
            break;
    }
}

static void draw_water_drops(GContext *ctx) {
    if (!s_is_watering) return;

    graphics_context_set_fill_color(ctx, COLOR_WATER);

    for (int i = 0; i < MAX_WATER_DROPS; i++) {
        if (s_drops[i].active) {
            graphics_fill_circle(ctx, s_drops[i].pos, s_drops[i].size);
        }
    }
}

static void draw_battery_indicator(GContext *ctx) {
    // Battery icon at top right
    int bat_x = s_screen_width - 26;
    int bat_y = 4;
    int bat_w = 20;
    int bat_h = 10;

    // Battery outline
    graphics_context_set_stroke_color(ctx, COLOR_TEXT);
    graphics_draw_rect(ctx, GRect(bat_x, bat_y, bat_w, bat_h));
    // Battery tip
    graphics_fill_rect(ctx, GRect(bat_x + bat_w, bat_y + 3, 2, 4), 0, GCornerNone);

    // Fill based on level
    int fill_w = (s_battery_level * (bat_w - 2)) / 100;
    if (fill_w > 0) {
        GColor bat_color = COLOR_TEXT;
        #ifdef PBL_COLOR
        if (s_battery_level <= 20) bat_color = GColorRed;
        else if (s_battery_level <= 40) bat_color = GColorOrange;
        else bat_color = GColorGreen;
        #endif
        graphics_context_set_fill_color(ctx, bat_color);
        graphics_fill_rect(ctx, GRect(bat_x + 1, bat_y + 1, fill_w, bat_h - 2), 0, GCornerNone);
    }
}

static void draw_water_indicator(GContext *ctx) {
    // Water level bar at bottom left
    int bar_width = 40;
    int bar_height = 8;
    int bar_x = 8;
    int bar_y = s_screen_height - 14;

    // Water drop icon
    #ifdef PBL_COLOR
    GColor drop_color = (s_plant.water_level < WATER_THIRSTY_MIN) ? GColorRed : GColorCyan;
    #else
    GColor drop_color = COLOR_TEXT;
    #endif
    graphics_context_set_fill_color(ctx, drop_color);
    graphics_fill_circle(ctx, GPoint(bar_x + 3, bar_y + 4), 4);

    // Bar outline
    graphics_context_set_stroke_color(ctx, COLOR_TEXT);
    GRect bar_outline = GRect(bar_x + 10, bar_y, bar_width, bar_height);
    graphics_draw_rect(ctx, bar_outline);

    // Fill based on water level
    int fill_width = (s_plant.water_level * (bar_width - 2)) / 100;
    if (fill_width > 0) {
        graphics_context_set_fill_color(ctx, drop_color);
        GRect fill = GRect(bar_x + 11, bar_y + 1, fill_width, bar_height - 2);
        graphics_fill_rect(ctx, fill, 0, GCornerNone);
    }

    // "Flex" hint at bottom right - wrist exercise to water!
    if (s_plant.water_level < WATER_HEALTHY_MIN) {
        graphics_context_set_text_color(ctx, COLOR_TEXT);
        const char *hint = (s_plant.water_level < WATER_THIRSTY_MIN) ? "FLEX!" : "flex";
        // Blink when urgent
        if (s_plant.water_level >= WATER_THIRSTY_MIN || (s_sway_phase / 8000) % 2 == 0) {
            graphics_draw_text(ctx, hint,
                fonts_get_system_font(FONT_KEY_GOTHIC_14),
                GRect(s_screen_width - 42, bar_y - 2, 38, 16),
                GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
        }
    }
}

static void draw_growth_progress(GContext *ctx) {
    // Small progress dots for growth to next stage
    if (s_plant.stage >= STAGE_FLOWERING) return;

    int dot_y = 58;  // Below date text
    int total_dots = 5;
    int filled_dots = (s_plant.growth_progress * total_dots) / GROWTH_TO_NEXT_STAGE;

    int dot_spacing = 8;
    int total_width = (total_dots - 1) * dot_spacing;
    int start_x = s_center_x - total_width / 2;

    for (int i = 0; i < total_dots; i++) {
        int dot_x = start_x + i * dot_spacing;
        if (i < filled_dots) {
            graphics_context_set_fill_color(ctx, COLOR_LEAF);
            graphics_fill_circle(ctx, GPoint(dot_x, dot_y), 3);
        } else {
            graphics_context_set_stroke_color(ctx, COLOR_TEXT);
            graphics_draw_circle(ctx, GPoint(dot_x, dot_y), 2);
        }
    }
}

// ============================================================================
// CANVAS UPDATE
// ============================================================================

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    // Sky background
    graphics_context_set_fill_color(ctx, COLOR_SKY);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    int y_base = s_screen_height - 5;

    // Draw pot first (background)
    draw_pot(ctx, y_base);

    // Draw plant
    draw_plant(ctx, y_base);

    // Draw water splash effect
    draw_water_drops(ctx);

    // Draw UI indicators
    draw_battery_indicator(ctx);
    draw_water_indicator(ctx);
    draw_growth_progress(ctx);
}

// ============================================================================
// ANIMATION UPDATE
// ============================================================================

static void update_animations(void) {
    // Update sway phase
    s_sway_phase = (s_sway_phase + 150) % TRIG_MAX_ANGLE;
    s_leaf_phase = (s_leaf_phase + 200) % TRIG_MAX_ANGLE;

    // Update wilt offset
    HealthState health = get_health_state();
    int target_wilt = 0;
    if (health == HEALTH_THIRSTY) target_wilt = 6;
    if (health == HEALTH_WILTING) target_wilt = 14;

    if (s_wilt_offset < target_wilt) s_wilt_offset++;
    else if (s_wilt_offset > target_wilt) s_wilt_offset--;

    // Update growth animation
    if (s_growth_anim > 0) s_growth_anim--;

    // Update water splash
    if (s_is_watering) {
        s_water_frame++;

        for (int i = 0; i < MAX_WATER_DROPS; i++) {
            if (s_drops[i].active) {
                s_drops[i].pos.x += s_drops[i].vel_x;
                s_drops[i].vel_y += 1;  // Gravity
                s_drops[i].pos.y += s_drops[i].vel_y;

                if (s_drops[i].pos.y > s_screen_height) {
                    s_drops[i].active = false;
                }
            }
        }

        if (s_water_frame >= 20) {
            s_is_watering = false;
        }
    }

    // Request redraw
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

static void animation_timer_callback(void *data) {
    update_animations();

    uint32_t interval = (s_battery_level <= LOW_BATTERY_THRESHOLD)
                        ? ANIMATION_INTERVAL_LOW_POWER
                        : ANIMATION_INTERVAL;

    s_animation_timer = app_timer_register(interval, animation_timer_callback, NULL);
}

// ============================================================================
// TIME HANDLING
// ============================================================================

static void update_time(void) {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    if (!tick_time || !s_time_layer) return;

    // Update time
    static char time_buffer[8];
    strftime(time_buffer, sizeof(time_buffer), "%I:%M", tick_time);

    // Remove leading zero
    if (time_buffer[0] == '0') {
        memmove(time_buffer, time_buffer + 1, strlen(time_buffer));
    }
    text_layer_set_text(s_time_layer, time_buffer);

    // Update date (e.g., "Fri Jan 17")
    if (s_date_layer) {
        static char date_buffer[16];
        strftime(date_buffer, sizeof(date_buffer), "%a %b %d", tick_time);
        text_layer_set_text(s_date_layer, date_buffer);
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();

    // Check water decay every minute
    if (s_plant.last_watered > 0) {
        time_t now = time(NULL);
        int32_t elapsed = now - s_plant.last_watered;
        int expected_level = WATER_MAX - ((elapsed / WATER_DECAY_INTERVAL) * WATER_DECAY_AMOUNT);
        if (expected_level < 0) expected_level = 0;

        if (s_plant.water_level > expected_level) {
            s_plant.water_level = expected_level;
            save_plant_state();
        }
    }

    // Check if plant has died and needs rebirth
    check_plant_death();
}

// ============================================================================
// BUTTON HANDLING
// ============================================================================

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    // Shake/tap to water the plant!
    water_plant();
}

// ============================================================================
// BATTERY HANDLING
// ============================================================================

static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
}

// ============================================================================
// WINDOW HANDLERS
// ============================================================================

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_screen_width = bounds.size.w;
    s_screen_height = bounds.size.h;
    s_center_x = s_screen_width / 2;

    // Canvas layer (full screen)
    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    // Time layer at top
    GRect time_frame = GRect(0, 2, s_screen_width, 40);
    s_time_layer = text_layer_create(time_frame);
    text_layer_set_text_color(s_time_layer, COLOR_TEXT);
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

    // Date layer below time
    GRect date_frame = GRect(0, 36, s_screen_width, 18);
    s_date_layer = text_layer_create(date_frame);
    text_layer_set_text_color(s_date_layer, COLOR_TEXT);
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

    // Initialize drops
    for (int i = 0; i < MAX_WATER_DROPS; i++) {
        s_drops[i].active = false;
    }

    // Start animation timer
    s_animation_timer = app_timer_register(ANIMATION_INTERVAL, animation_timer_callback, NULL);

    // Initial time update
    update_time();
}

static void main_window_unload(Window *window) {
    if (s_animation_timer) {
        app_timer_cancel(s_animation_timer);
        s_animation_timer = NULL;
    }

    save_plant_state();

    if (s_canvas_layer) {
        layer_destroy(s_canvas_layer);
        s_canvas_layer = NULL;
    }
    if (s_time_layer) {
        text_layer_destroy(s_time_layer);
        s_time_layer = NULL;
    }
    if (s_date_layer) {
        text_layer_destroy(s_date_layer);
        s_date_layer = NULL;
    }
}

// ============================================================================
// APPLICATION LIFECYCLE
// ============================================================================

static void init(void) {
    srand(time(NULL));

    load_plant_state();

    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
    accel_tap_service_subscribe(accel_tap_handler);  // Shake to water!

    BatteryChargeState state = battery_state_service_peek();
    s_battery_level = state.charge_percent;
}

static void deinit(void) {
    save_plant_state();

    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    accel_tap_service_unsubscribe();

    if (s_main_window) {
        window_destroy(s_main_window);
        s_main_window = NULL;
    }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
