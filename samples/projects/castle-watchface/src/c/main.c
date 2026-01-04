#include <pebble.h>

// Screen dimensions
#define SCREEN_WIDTH 144
#define SCREEN_HEIGHT 168

// Layout zones
#define SKY_HEIGHT 55
#define GROUND_TOP 138
#define GROUND_HEIGHT (SCREEN_HEIGHT - GROUND_TOP)

// Castle dimensions
#define CASTLE_BASE_Y 138
#define CASTLE_WIDTH 80
#define TOWER_WIDTH 18
#define TOWER_HEIGHT 70
#define KEEP_HEIGHT 50
#define BATTLEMENT_HEIGHT 8

// Knight dimensions
#define KNIGHT_WIDTH 12
#define KNIGHT_HEIGHT 18
#define KNIGHT_Y (GROUND_TOP + 10)

// Animation
#define ANIMATION_INTERVAL 80
#define LOW_POWER_INTERVAL 150
#define LOW_BATTERY_THRESHOLD 20

// Knight states
typedef struct {
    int x;
    int direction;  // 1 = right, -1 = left
    int leg_phase;  // For walking animation
    bool active;
} Knight;

// Global variables
static Window *s_main_window;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_day_layer;
static Layer *s_battery_layer;
static AppTimer *s_animation_timer = NULL;

static Knight s_knights[2];
static int s_battery_level = 100;
static int s_star_positions[8];  // Pre-computed star positions

// Forward declarations
static void animation_timer_callback(void *data);

// Initialize knights
static void init_knights(void) {
    // Knight 1: starts left, walks right
    s_knights[0].x = 10;
    s_knights[0].direction = 1;
    s_knights[0].leg_phase = 0;
    s_knights[0].active = true;

    // Knight 2: starts right, walks left
    s_knights[1].x = SCREEN_WIDTH - 25;
    s_knights[1].direction = -1;
    s_knights[1].leg_phase = 4;
    s_knights[1].active = true;
}

// Initialize star positions
static void init_stars(void) {
    srand(42);  // Fixed seed for consistent star pattern
    for (int i = 0; i < 8; i++) {
        s_star_positions[i] = (rand() % (SCREEN_WIDTH - 10)) + 5;
    }
}

// Update knight positions
static void update_knights(void) {
    for (int i = 0; i < 2; i++) {
        Knight *k = &s_knights[i];
        if (!k->active) continue;

        // Move knight
        k->x += k->direction * 1;

        // Bounce at edges (patrol behavior)
        if (k->x <= 5) {
            k->x = 5;
            k->direction = 1;
        } else if (k->x >= SCREEN_WIDTH - 20) {
            k->x = SCREEN_WIDTH - 20;
            k->direction = -1;
        }

        // Update leg animation phase
        k->leg_phase = (k->leg_phase + 1) % 8;
    }
}

// Draw a single star
static void draw_star(GContext *ctx, int x, int y) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_pixel(ctx, GPoint(x, y));
    graphics_draw_pixel(ctx, GPoint(x-1, y));
    graphics_draw_pixel(ctx, GPoint(x+1, y));
    graphics_draw_pixel(ctx, GPoint(x, y-1));
    graphics_draw_pixel(ctx, GPoint(x, y+1));
}

// Draw sky with stars
static void draw_sky(GContext *ctx) {
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    #else
    graphics_context_set_fill_color(ctx, GColorBlack);
    #endif
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_WIDTH, SKY_HEIGHT + 20), 0, GCornerNone);

    // Draw stars
    int star_y[] = {8, 15, 12, 20, 10, 18, 25, 14};
    for (int i = 0; i < 8; i++) {
        draw_star(ctx, s_star_positions[i], star_y[i]);
    }
}

// Draw ground
static void draw_ground(GContext *ctx) {
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorDarkGreen);
    #else
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    #endif
    graphics_fill_rect(ctx, GRect(0, GROUND_TOP, SCREEN_WIDTH, GROUND_HEIGHT), 0, GCornerNone);
}

// Draw castle tower
static void draw_tower(GContext *ctx, int center_x, int base_y, int height) {
    int half_width = TOWER_WIDTH / 2;

    // Tower body
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorLightGray);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_rect(ctx, GRect(center_x - half_width, base_y - height, TOWER_WIDTH, height), 0, GCornerNone);

    // Tower outline
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #else
    graphics_context_set_stroke_color(ctx, GColorBlack);
    #endif
    graphics_draw_rect(ctx, GRect(center_x - half_width, base_y - height, TOWER_WIDTH, height));

    // Battlements on top
    int battlement_y = base_y - height - BATTLEMENT_HEIGHT;
    for (int i = 0; i < 3; i++) {
        int bx = center_x - half_width + 2 + (i * 6);
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorLightGray);
        #else
        graphics_context_set_fill_color(ctx, GColorWhite);
        #endif
        graphics_fill_rect(ctx, GRect(bx, battlement_y, 4, BATTLEMENT_HEIGHT), 0, GCornerNone);
    }

    // Window
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(center_x - 3, base_y - height + 15, 6, 10), 0, GCornerNone);
}

// Draw main castle keep
static void draw_keep(GContext *ctx) {
    int center_x = SCREEN_WIDTH / 2;
    int keep_width = CASTLE_WIDTH - TOWER_WIDTH * 2;
    int keep_x = center_x - keep_width / 2;

    // Keep body
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorLightGray);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_rect(ctx, GRect(keep_x, CASTLE_BASE_Y - KEEP_HEIGHT, keep_width, KEEP_HEIGHT), 0, GCornerNone);

    // Keep outline
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #else
    graphics_context_set_stroke_color(ctx, GColorBlack);
    #endif
    graphics_draw_rect(ctx, GRect(keep_x, CASTLE_BASE_Y - KEEP_HEIGHT, keep_width, KEEP_HEIGHT));

    // Battlements on keep
    int battlement_y = CASTLE_BASE_Y - KEEP_HEIGHT - BATTLEMENT_HEIGHT;
    for (int i = 0; i < 6; i++) {
        int bx = keep_x + 3 + (i * 7);
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorLightGray);
        #else
        graphics_context_set_fill_color(ctx, GColorWhite);
        #endif
        graphics_fill_rect(ctx, GRect(bx, battlement_y, 4, BATTLEMENT_HEIGHT), 0, GCornerNone);
    }

    // Gate (arched door)
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    #else
    graphics_context_set_fill_color(ctx, GColorBlack);
    #endif
    // Door rectangle
    graphics_fill_rect(ctx, GRect(center_x - 8, CASTLE_BASE_Y - 25, 16, 25), 0, GCornerNone);
    // Arch top
    graphics_fill_circle(ctx, GPoint(center_x, CASTLE_BASE_Y - 25), 8);

    // Gate bars
    graphics_context_set_stroke_color(ctx, GColorBlack);
    for (int i = 0; i < 3; i++) {
        int gx = center_x - 5 + (i * 5);
        graphics_draw_line(ctx, GPoint(gx, CASTLE_BASE_Y - 22), GPoint(gx, CASTLE_BASE_Y));
    }
}

// Draw complete castle
static void draw_castle(GContext *ctx) {
    int center_x = SCREEN_WIDTH / 2;

    // Left tower
    draw_tower(ctx, center_x - 30, CASTLE_BASE_Y, TOWER_HEIGHT);

    // Right tower
    draw_tower(ctx, center_x + 30, CASTLE_BASE_Y, TOWER_HEIGHT);

    // Main keep
    draw_keep(ctx);
}

// Draw a knight
static void draw_knight(GContext *ctx, Knight *knight) {
    if (!knight->active) return;

    int x = knight->x;
    int y = KNIGHT_Y;
    int dir = knight->direction;

    // Leg animation offset
    int leg_offset = (knight->leg_phase < 4) ? 2 : -2;

    // Body (armor)
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_rect(ctx, GRect(x + 2, y + 6, 8, 8), 0, GCornerNone);

    // Head (helmet)
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorLightGray);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_circle(ctx, GPoint(x + 6, y + 3), 4);

    // Helmet visor
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(x + 4, y + 3), GPoint(x + 8, y + 3));

    // Legs
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    #else
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    #endif
    graphics_fill_rect(ctx, GRect(x + 2 + leg_offset, y + 14, 3, 4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(x + 7 - leg_offset, y + 14, 3, 4), 0, GCornerNone);

    // Shield
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorRed);
    #else
    graphics_context_set_fill_color(ctx, GColorBlack);
    #endif
    if (dir == 1) {
        graphics_fill_rect(ctx, GRect(x, y + 7, 3, 6), 0, GCornerNone);
    } else {
        graphics_fill_rect(ctx, GRect(x + 9, y + 7, 3, 6), 0, GCornerNone);
    }

    // Sword
    graphics_context_set_stroke_color(ctx, GColorWhite);
    if (dir == 1) {
        graphics_draw_line(ctx, GPoint(x + 10, y + 5), GPoint(x + 15, y + 2));
    } else {
        graphics_draw_line(ctx, GPoint(x + 2, y + 5), GPoint(x - 3, y + 2));
    }
}

// Canvas update proc
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    // Draw background elements
    draw_sky(ctx);
    draw_ground(ctx);

    // Draw castle
    draw_castle(ctx);

    // Draw knights
    for (int i = 0; i < 2; i++) {
        draw_knight(ctx, &s_knights[i]);
    }
}

// Battery layer update proc
static void battery_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    // Battery outline
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, GRect(0, 0, bounds.size.w - 2, bounds.size.h));

    // Battery tip
    graphics_fill_rect(ctx, GRect(bounds.size.w - 2, 2, 2, bounds.size.h - 4), 0, GCornerNone);

    // Fill based on level
    int fill_width = ((bounds.size.w - 4) * s_battery_level) / 100;

    #ifdef PBL_COLOR
    if (s_battery_level <= 20) {
        graphics_context_set_fill_color(ctx, GColorRed);
    } else if (s_battery_level <= 50) {
        graphics_context_set_fill_color(ctx, GColorYellow);
    } else {
        graphics_context_set_fill_color(ctx, GColorGreen);
    }
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif

    graphics_fill_rect(ctx, GRect(2, 2, fill_width, bounds.size.h - 4), 0, GCornerNone);
}

// Update time display
static void update_time(void) {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    static char time_buffer[8];
    strftime(time_buffer, sizeof(time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

    // Remove leading zero for 12-hour format
    if (!clock_is_24h_style() && time_buffer[0] == '0') {
        memmove(time_buffer, time_buffer + 1, strlen(time_buffer));
    }

    text_layer_set_text(s_time_layer, time_buffer);

    static char day_buffer[16];
    strftime(day_buffer, sizeof(day_buffer), "%a, %b %d", tick_time);
    text_layer_set_text(s_day_layer, day_buffer);
}

// Tick handler
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
}

// Battery callback
static void battery_callback(BatteryChargeState charge_state) {
    s_battery_level = charge_state.charge_percent;
    if (s_battery_layer) {
        layer_mark_dirty(s_battery_layer);
    }
}

// Animation timer callback
static void animation_timer_callback(void *data) {
    update_knights();
    layer_mark_dirty(s_canvas_layer);

    // Adaptive frame rate
    uint32_t interval = (s_battery_level <= LOW_BATTERY_THRESHOLD)
                        ? LOW_POWER_INTERVAL
                        : ANIMATION_INTERVAL;

    s_animation_timer = app_timer_register(interval, animation_timer_callback, NULL);
}

// Window load
static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // Set window background
    window_set_background_color(window, GColorBlack);

    // Create canvas layer for drawing
    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    // Time layer - positioned at very top above castle
    s_time_layer = text_layer_create(GRect(0, 5, bounds.size.w, 34));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

    // Day layer - below time
    s_day_layer = text_layer_create(GRect(0, 35, bounds.size.w, 18));
    text_layer_set_background_color(s_day_layer, GColorClear);
    text_layer_set_text_color(s_day_layer, GColorWhite);
    text_layer_set_font(s_day_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_day_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_day_layer));

    // Battery layer
    s_battery_layer = layer_create(GRect(bounds.size.w - 28, 5, 24, 10));
    layer_set_update_proc(s_battery_layer, battery_update_proc);
    layer_add_child(window_layer, s_battery_layer);

    // Initialize elements
    init_stars();
    init_knights();

    // Update time immediately
    update_time();

    // Get initial battery level
    battery_callback(battery_state_service_peek());

    // Start animation timer
    s_animation_timer = app_timer_register(ANIMATION_INTERVAL, animation_timer_callback, NULL);
}

// Window unload
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

    if (s_day_layer) {
        text_layer_destroy(s_day_layer);
        s_day_layer = NULL;
    }

    if (s_battery_layer) {
        layer_destroy(s_battery_layer);
        s_battery_layer = NULL;
    }
}

// Init
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

// Deinit
static void deinit(void) {
    // Cancel animation timer
    if (s_animation_timer) {
        app_timer_cancel(s_animation_timer);
        s_animation_timer = NULL;
    }

    // Unsubscribe services
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();

    // Destroy window
    if (s_main_window) {
        window_destroy(s_main_window);
        s_main_window = NULL;
    }
}

// Main
int main(void) {
    init();
    app_event_loop();
    deinit();
}
