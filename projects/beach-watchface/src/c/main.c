#include <pebble.h>

// ============================================================================
// CONSTANTS
// ============================================================================
#define NUM_WAVES 3
#define ANIMATION_INTERVAL 50
#define ANIMATION_INTERVAL_LOW_POWER 100
#define LOW_BATTERY_THRESHOLD 20

#define SCREEN_WIDTH 144
#define SCREEN_HEIGHT 168

#define SKY_START_Y 0
#define SKY_END_Y 55
#define OCEAN_START_Y 90
#define OCEAN_END_Y 140
#define SAND_START_Y 140

#define SUN_CENTER_X 115
#define SUN_CENTER_Y 28
#define SUN_RADIUS 14
#define SUN_RAY_LENGTH 10
#define SUN_NUM_RAYS 8

// ============================================================================
// COLOR DEFINITIONS
// ============================================================================
#ifdef PBL_COLOR
    #define COLOR_SKY GColorPictonBlue
    #define COLOR_SKY_LIGHT GColorCeleste
    #define COLOR_SUN GColorYellow
    #define COLOR_SUN_RAYS GColorOrange
    #define COLOR_OCEAN_DEEP GColorCobaltBlue
    #define COLOR_OCEAN_MID GColorBlue
    #define COLOR_OCEAN_LIGHT GColorPictonBlue
    #define COLOR_SAND GColorFromRGB(210, 180, 140)
    #define COLOR_SAND_DARK GColorFromRGB(180, 150, 110)
    #define COLOR_TEXT GColorWhite
    #define COLOR_TEXT_SHADOW GColorBlack
#else
    #define COLOR_SKY GColorWhite
    #define COLOR_SKY_LIGHT GColorWhite
    #define COLOR_SUN GColorWhite
    #define COLOR_SUN_RAYS GColorWhite
    #define COLOR_OCEAN_DEEP GColorBlack
    #define COLOR_OCEAN_MID GColorBlack
    #define COLOR_OCEAN_LIGHT GColorWhite
    #define COLOR_SAND GColorLightGray
    #define COLOR_SAND_DARK GColorDarkGray
    #define COLOR_TEXT GColorBlack
    #define COLOR_TEXT_SHADOW GColorWhite
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================
typedef struct {
    int base_y;
    int32_t phase;
    int amplitude;
    int speed;
    GColor color;
} Wave;

typedef struct {
    GPoint center;
    int radius;
    int ray_length;
    int num_rays;
} Sun;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
static Window *s_main_window;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static AppTimer *s_animation_timer;

static int s_battery_level = 100;
static bool s_is_charging = false;

static Wave s_waves[NUM_WAVES];
static Sun s_sun;

static char s_time_buffer[8];
static char s_date_buffer[16];

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================
static void init_waves(void) {
    // Wave 1 - Front wave (closest, fastest, lightest)
    s_waves[0].base_y = 128;
    s_waves[0].phase = 0;
    s_waves[0].amplitude = 5;
    s_waves[0].speed = 450;
    #ifdef PBL_COLOR
    s_waves[0].color = GColorPictonBlue;
    #else
    s_waves[0].color = GColorWhite;
    #endif

    // Wave 2 - Middle wave
    s_waves[1].base_y = 115;
    s_waves[1].phase = TRIG_MAX_ANGLE / 3;
    s_waves[1].amplitude = 6;
    s_waves[1].speed = 320;
    #ifdef PBL_COLOR
    s_waves[1].color = GColorVividCerulean;
    #else
    s_waves[1].color = GColorLightGray;
    #endif

    // Wave 3 - Back wave (farthest, slowest, darkest)
    s_waves[2].base_y = 102;
    s_waves[2].phase = TRIG_MAX_ANGLE * 2 / 3;
    s_waves[2].amplitude = 4;
    s_waves[2].speed = 220;
    #ifdef PBL_COLOR
    s_waves[2].color = GColorCobaltBlue;
    #else
    s_waves[2].color = GColorDarkGray;
    #endif
}

static void init_sun(void) {
    s_sun.center.x = SUN_CENTER_X;
    s_sun.center.y = SUN_CENTER_Y;
    s_sun.radius = SUN_RADIUS;
    s_sun.ray_length = SUN_RAY_LENGTH;
    s_sun.num_rays = SUN_NUM_RAYS;
}

// ============================================================================
// DRAWING FUNCTIONS
// ============================================================================
static void draw_sun(GContext *ctx) {
    // Draw sun glow (slightly larger circle behind)
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorRajah);
    graphics_fill_circle(ctx, s_sun.center, s_sun.radius + 3);
    #endif

    // Draw sun body
    graphics_context_set_fill_color(ctx, COLOR_SUN);
    graphics_fill_circle(ctx, s_sun.center, s_sun.radius);

    // Draw sun rays
    graphics_context_set_stroke_color(ctx, COLOR_SUN_RAYS);
    graphics_context_set_stroke_width(ctx, 2);

    for (int i = 0; i < s_sun.num_rays; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE / s_sun.num_rays);

        int inner_radius = s_sun.radius + 4;
        int outer_radius = s_sun.radius + s_sun.ray_length;

        GPoint ray_start = {
            s_sun.center.x + (sin_lookup(angle) * inner_radius) / TRIG_MAX_RATIO,
            s_sun.center.y - (cos_lookup(angle) * inner_radius) / TRIG_MAX_RATIO
        };

        GPoint ray_end = {
            s_sun.center.x + (sin_lookup(angle) * outer_radius) / TRIG_MAX_RATIO,
            s_sun.center.y - (cos_lookup(angle) * outer_radius) / TRIG_MAX_RATIO
        };

        graphics_draw_line(ctx, ray_start, ray_end);
    }
}

static void draw_sky(GContext *ctx) {
    // Main sky
    graphics_context_set_fill_color(ctx, COLOR_SKY);
    GRect sky_rect = GRect(0, SKY_START_Y, SCREEN_WIDTH, SKY_END_Y - SKY_START_Y);
    graphics_fill_rect(ctx, sky_rect, 0, GCornerNone);

    // Lighter band near horizon
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, COLOR_SKY_LIGHT);
    GRect horizon_band = GRect(0, SKY_END_Y - 15, SCREEN_WIDTH, 15);
    graphics_fill_rect(ctx, horizon_band, 0, GCornerNone);
    #endif

    // Draw the sun
    draw_sun(ctx);
}

static void draw_ocean_background(GContext *ctx) {
    // Fill ocean zone with gradient-like effect
    #ifdef PBL_COLOR
    // Lighter blue at top (near horizon)
    graphics_context_set_fill_color(ctx, GColorVividCerulean);
    GRect ocean_top = GRect(0, SKY_END_Y, SCREEN_WIDTH, 20);
    graphics_fill_rect(ctx, ocean_top, 0, GCornerNone);

    // Mid blue
    graphics_context_set_fill_color(ctx, COLOR_OCEAN_MID);
    GRect ocean_mid = GRect(0, SKY_END_Y + 20, SCREEN_WIDTH, 30);
    graphics_fill_rect(ctx, ocean_mid, 0, GCornerNone);

    // Deeper blue
    graphics_context_set_fill_color(ctx, COLOR_OCEAN_DEEP);
    GRect ocean_deep = GRect(0, SKY_END_Y + 50, SCREEN_WIDTH, SAND_START_Y - SKY_END_Y - 50);
    graphics_fill_rect(ctx, ocean_deep, 0, GCornerNone);
    #else
    graphics_context_set_fill_color(ctx, GColorBlack);
    GRect ocean_rect = GRect(0, SKY_END_Y, SCREEN_WIDTH, SAND_START_Y - SKY_END_Y);
    graphics_fill_rect(ctx, ocean_rect, 0, GCornerNone);
    #endif
}

static void draw_wave(GContext *ctx, const Wave *wave) {
    graphics_context_set_stroke_color(ctx, wave->color);
    graphics_context_set_stroke_width(ctx, 2);

    GPoint prev, curr;

    // First point
    int32_t angle = wave->phase;
    int16_t y_offset = (sin_lookup(angle) * wave->amplitude) / TRIG_MAX_RATIO;
    prev.x = 0;
    prev.y = wave->base_y + y_offset;

    // Draw wave as connected line segments
    for (int x = 6; x <= SCREEN_WIDTH; x += 6) {
        int32_t segment_angle = (wave->phase + (x * TRIG_MAX_ANGLE * 2 / SCREEN_WIDTH)) % TRIG_MAX_ANGLE;
        y_offset = (sin_lookup(segment_angle) * wave->amplitude) / TRIG_MAX_RATIO;

        curr.x = x;
        curr.y = wave->base_y + y_offset;

        graphics_draw_line(ctx, prev, curr);
        prev = curr;
    }
}

static void draw_sand(GContext *ctx) {
    // Main sand
    graphics_context_set_fill_color(ctx, COLOR_SAND);
    GRect sand_rect = GRect(0, SAND_START_Y, SCREEN_WIDTH, SCREEN_HEIGHT - SAND_START_Y);
    graphics_fill_rect(ctx, sand_rect, 0, GCornerNone);

    // Add some texture dots
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, COLOR_SAND_DARK);
    int x_pos[] = {10, 28, 45, 62, 85, 100, 120, 135, 15, 55, 90, 110};
    int y_off[] = {4, 12, 8, 18, 6, 14, 10, 20, 22, 16, 25, 5};
    for (int i = 0; i < 12; i++) {
        graphics_fill_circle(ctx, GPoint(x_pos[i], SAND_START_Y + y_off[i]), 1);
    }
    #endif
}

static void draw_battery(GContext *ctx, GRect bounds) {
    // Battery outline
    graphics_context_set_stroke_color(ctx, COLOR_TEXT);
    GRect battery_body = GRect(0, 0, bounds.size.w - 2, bounds.size.h);
    graphics_draw_rect(ctx, battery_body);

    // Battery tip
    GRect battery_tip = GRect(bounds.size.w - 2, 2, 2, bounds.size.h - 4);
    graphics_context_set_fill_color(ctx, COLOR_TEXT);
    graphics_fill_rect(ctx, battery_tip, 0, GCornerNone);

    // Battery fill
    int fill_width = (s_battery_level * (bounds.size.w - 4)) / 100;
    if (fill_width > 0) {
        GRect fill_rect = GRect(2, 2, fill_width, bounds.size.h - 4);

        #ifdef PBL_COLOR
        if (s_battery_level <= 20) {
            graphics_context_set_fill_color(ctx, GColorRed);
        } else if (s_battery_level <= 40) {
            graphics_context_set_fill_color(ctx, GColorOrange);
        } else {
            graphics_context_set_fill_color(ctx, GColorGreen);
        }
        #else
        graphics_context_set_fill_color(ctx, COLOR_TEXT);
        #endif

        graphics_fill_rect(ctx, fill_rect, 0, GCornerNone);
    }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    // Draw background elements (back to front)
    draw_sky(ctx);
    draw_ocean_background(ctx);

    // Draw waves from back to front
    for (int i = NUM_WAVES - 1; i >= 0; i--) {
        draw_wave(ctx, &s_waves[i]);
    }

    // Draw sand
    draw_sand(ctx);

    // Draw battery indicator in top-right corner
    GRect battery_bounds = GRect(bounds.size.w - 28, 5, 24, 10);
    GContext *battery_ctx = ctx;
    graphics_context_set_fill_color(battery_ctx, GColorClear);
    draw_battery(battery_ctx, battery_bounds);
}

// ============================================================================
// UPDATE FUNCTIONS
// ============================================================================
static void update_waves(void) {
    for (int i = 0; i < NUM_WAVES; i++) {
        s_waves[i].phase = (s_waves[i].phase + s_waves[i].speed) % TRIG_MAX_ANGLE;
    }
}

static void update_time(void) {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    // Format time
    strftime(s_time_buffer, sizeof(s_time_buffer),
             clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

    // Remove leading zero for 12-hour format
    if (!clock_is_24h_style() && s_time_buffer[0] == '0') {
        memmove(s_time_buffer, &s_time_buffer[1], sizeof(s_time_buffer) - 1);
    }

    // Format date
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a, %b %d", tick_time);

    // Update text layers
    if (s_time_layer) {
        text_layer_set_text(s_time_layer, s_time_buffer);
    }
    if (s_date_layer) {
        text_layer_set_text(s_date_layer, s_date_buffer);
    }
}

// ============================================================================
// CALLBACK HANDLERS
// ============================================================================
static void animation_timer_callback(void *data) {
    update_waves();

    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }

    uint32_t next_interval = (s_battery_level <= LOW_BATTERY_THRESHOLD && !s_is_charging)
                             ? ANIMATION_INTERVAL_LOW_POWER
                             : ANIMATION_INTERVAL;

    s_animation_timer = app_timer_register(next_interval, animation_timer_callback, NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
}

static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
    s_is_charging = state.is_charging;

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

    // Canvas layer for background and waves
    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    // Time text layer
    s_time_layer = text_layer_create(GRect(0, 52, bounds.size.w, 50));
    text_layer_set_text_color(s_time_layer, COLOR_TEXT);
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

    // Date text layer
    s_date_layer = text_layer_create(GRect(0, 40, bounds.size.w, 20));
    text_layer_set_text_color(s_date_layer, COLOR_TEXT);
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

    // Initialize elements
    init_waves();
    init_sun();

    // Get initial battery state
    BatteryChargeState charge = battery_state_service_peek();
    s_battery_level = charge.charge_percent;
    s_is_charging = charge.is_charging;

    // Start animation
    s_animation_timer = app_timer_register(ANIMATION_INTERVAL, animation_timer_callback, NULL);

    // Initial time update
    update_time();
}

static void main_window_unload(Window *window) {
    // Cancel animation timer
    if (s_animation_timer) {
        app_timer_cancel(s_animation_timer);
        s_animation_timer = NULL;
    }

    // Destroy layers
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
    s_main_window = window_create();

    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });

    window_stack_push(s_main_window, true);

    // Subscribe to services
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
}

static void deinit(void) {
    // Unsubscribe from services
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();

    // Destroy window
    if (s_main_window) {
        window_destroy(s_main_window);
        s_main_window = NULL;
    }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
