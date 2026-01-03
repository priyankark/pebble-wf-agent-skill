# Pebble API Reference

## Core Header
```c
#include <pebble.h>
```

## Data Types

### Points and Rectangles
```c
typedef struct {
    int16_t x;
    int16_t y;
} GPoint;

typedef struct {
    GPoint origin;
    GSize size;
} GRect;

typedef struct {
    int16_t w;
    int16_t h;
} GSize;
```

### Colors
```c
// Basic colors (work on all platforms)
GColorBlack
GColorWhite
GColorClear  // Transparent

// Color display colors (basalt, chalk only)
GColorRed, GColorGreen, GColorBlue
GColorYellow, GColorCyan, GColorMagenta
GColorOrange, GColorPurple, GColorPink
// ... and many more (64 colors total)

// Create custom color
GColorFromRGB(r, g, b)  // r, g, b: 0-255
GColorFromHEX(0xRRGGBB)
```

## Graphics Context

### Setting Colors
```c
graphics_context_set_fill_color(GContext *ctx, GColor color);
graphics_context_set_stroke_color(GContext *ctx, GColor color);
graphics_context_set_stroke_width(GContext *ctx, uint8_t width);
graphics_context_set_text_color(GContext *ctx, GColor color);
```

### Drawing Shapes
```c
// Circles
graphics_fill_circle(GContext *ctx, GPoint center, uint16_t radius);
graphics_draw_circle(GContext *ctx, GPoint center, uint16_t radius);

// Rectangles
graphics_fill_rect(GContext *ctx, GRect rect, uint16_t corner_radius, GCornerMask corners);
graphics_draw_rect(GContext *ctx, GRect rect);

// Lines
graphics_draw_line(GContext *ctx, GPoint start, GPoint end);

// Pixels
graphics_draw_pixel(GContext *ctx, GPoint point);
```

### Corner Masks for Rectangles
```c
GCornerNone
GCornersAll
GCornersTop
GCornersBottom
GCornersLeft
GCornersRight
GCornerTopLeft
GCornerTopRight
GCornerBottomLeft
GCornerBottomRight
```

## Paths (Vector Graphics)

### Creating Paths
```c
// Define path info structure
static const GPathInfo PATH_INFO = {
    .num_points = 3,
    .points = (GPoint[]) {
        {0, 0},
        {10, 20},
        {20, 0}
    }
};

// Create path (do this once in window_load, not in update_proc!)
GPath *path = gpath_create(&PATH_INFO);

// Or with dynamic points
static GPoint points[3];
static GPathInfo path_info = {
    .num_points = 3,
    .points = points
};
GPath *path = gpath_create(&path_info);
```

### Drawing Paths
```c
gpath_draw_filled(GContext *ctx, GPath *path);
gpath_draw_outline(GContext *ctx, GPath *path);
```

### Transforming Paths
```c
gpath_move_to(GPath *path, GPoint point);
gpath_rotate_to(GPath *path, int32_t angle);  // angle in TRIG_MAX_ANGLE units
```

### Destroying Paths
```c
gpath_destroy(GPath *path);  // Call in window_unload!
```

## Trigonometry (Fixed-Point)

**IMPORTANT**: Pebble uses fixed-point math. No floating point!

### Constants
```c
TRIG_MAX_ANGLE  // Full circle = 65536 (0x10000)
TRIG_MAX_RATIO  // Maximum sin/cos value = 65536

// Useful conversions
#define DEG_TO_TRIG(deg) ((deg) * TRIG_MAX_ANGLE / 360)
```

### Functions
```c
// Returns value in [-TRIG_MAX_RATIO, TRIG_MAX_RATIO]
int32_t sin_lookup(int32_t angle);
int32_t cos_lookup(int32_t angle);

// Inverse trigonometry
int32_t atan2_lookup(int16_t y, int16_t x);
```

### Usage Example
```c
// Calculate point on circle
int32_t angle = (hour * TRIG_MAX_ANGLE) / 12;  // Hour hand angle
int16_t x = center.x + (sin_lookup(angle) * radius) / TRIG_MAX_RATIO;
int16_t y = center.y - (cos_lookup(angle) * radius) / TRIG_MAX_RATIO;
```

## Layers

### Window Layer
```c
Layer *window_get_root_layer(Window *window);
GRect layer_get_bounds(Layer *layer);
```

### Custom Layers
```c
Layer *layer_create(GRect frame);
void layer_destroy(Layer *layer);
void layer_set_update_proc(Layer *layer, LayerUpdateProc update_proc);
void layer_add_child(Layer *parent, Layer *child);
void layer_mark_dirty(Layer *layer);  // Request redraw
```

### Update Procedure
```c
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    // Draw here...
}
```

### Text Layers
```c
TextLayer *text_layer_create(GRect frame);
void text_layer_destroy(TextLayer *layer);
void text_layer_set_text(TextLayer *layer, const char *text);
void text_layer_set_font(TextLayer *layer, GFont font);
void text_layer_set_text_color(TextLayer *layer, GColor color);
void text_layer_set_background_color(TextLayer *layer, GColor color);
void text_layer_set_text_alignment(TextLayer *layer, GTextAlignment alignment);
Layer *text_layer_get_layer(TextLayer *layer);
```

### Text Alignment
```c
GTextAlignmentLeft
GTextAlignmentCenter
GTextAlignmentRight
```

## Fonts

### System Fonts
```c
GFont fonts_get_system_font(const char *font_key);

// Common font keys
FONT_KEY_GOTHIC_14
FONT_KEY_GOTHIC_14_BOLD
FONT_KEY_GOTHIC_18
FONT_KEY_GOTHIC_18_BOLD
FONT_KEY_GOTHIC_24
FONT_KEY_GOTHIC_24_BOLD
FONT_KEY_GOTHIC_28
FONT_KEY_GOTHIC_28_BOLD
FONT_KEY_BITHAM_30_BLACK
FONT_KEY_BITHAM_42_BOLD
FONT_KEY_BITHAM_42_LIGHT
FONT_KEY_ROBOTO_CONDENSED_21
FONT_KEY_LECO_20_BOLD_NUMBERS
FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM
FONT_KEY_LECO_32_BOLD_NUMBERS
FONT_KEY_LECO_36_BOLD_NUMBERS
FONT_KEY_LECO_38_BOLD_NUMBERS
FONT_KEY_LECO_42_NUMBERS
```

## Windows

### Creating Windows
```c
Window *window_create(void);
void window_destroy(Window *window);
void window_set_window_handlers(Window *window, WindowHandlers handlers);
void window_stack_push(Window *window, bool animated);
```

### Window Handlers
```c
window_set_window_handlers(window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
});
```

## Time

### Getting Current Time
```c
time_t temp = time(NULL);
struct tm *tick_time = localtime(&temp);

// Access components
tick_time->tm_hour   // 0-23
tick_time->tm_min    // 0-59
tick_time->tm_sec    // 0-59
tick_time->tm_mday   // 1-31 (day of month)
tick_time->tm_mon    // 0-11 (month)
tick_time->tm_year   // Years since 1900
tick_time->tm_wday   // 0-6 (day of week, Sunday = 0)
```

### Time Formatting
```c
static char buffer[8];
strftime(buffer, sizeof(buffer), "%H:%M", tick_time);  // 24-hour
strftime(buffer, sizeof(buffer), "%I:%M", tick_time);  // 12-hour

// Common format codes
// %H - 24-hour (00-23)
// %I - 12-hour (01-12)
// %M - Minutes (00-59)
// %S - Seconds (00-59)
// %p - AM/PM
// %a - Abbreviated weekday (Mon, Tue...)
// %A - Full weekday
// %b - Abbreviated month (Jan, Feb...)
// %B - Full month
// %d - Day of month (01-31)
// %m - Month (01-12)
// %Y - Year (2024)
```

### Tick Timer Service
```c
void tick_timer_service_subscribe(TimeUnits units, TickHandler handler);
void tick_timer_service_unsubscribe(void);

// TimeUnits
SECOND_UNIT
MINUTE_UNIT
HOUR_UNIT
DAY_UNIT
MONTH_UNIT
YEAR_UNIT

// Handler
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
}
```

## App Timers (for Animation)

```c
AppTimer *app_timer_register(uint32_t timeout_ms, AppTimerCallback callback, void *data);
void app_timer_cancel(AppTimer *timer);
bool app_timer_reschedule(AppTimer *timer, uint32_t new_timeout_ms);

// Callback
static void timer_callback(void *data) {
    // Update animation
    layer_mark_dirty(s_canvas_layer);

    // Re-register for next frame
    s_timer = app_timer_register(50, timer_callback, NULL);
}
```

## Battery Service

```c
BatteryChargeState battery_state_service_peek(void);
void battery_state_service_subscribe(BatteryStateHandler handler);
void battery_state_service_unsubscribe(void);

// BatteryChargeState
typedef struct {
    uint8_t charge_percent;  // 0-100
    bool is_charging;
    bool is_plugged;
} BatteryChargeState;

// Handler
static void battery_callback(BatteryChargeState charge_state) {
    s_battery_level = charge_state.charge_percent;
}
```

## Random Numbers

```c
#include <stdlib.h>

srand(time(NULL));  // Seed once in init()
int value = rand() % range;  // Get random number
```

### Safe Random in Range
```c
static int random_in_range(int min, int max) {
    if (max <= min) return min;
    int range = max - min + 1;
    return min + (rand() % range);
}
```

## Logging

```c
APP_LOG(APP_LOG_LEVEL_DEBUG, "Debug message: %d", value);
APP_LOG(APP_LOG_LEVEL_INFO, "Info message");
APP_LOG(APP_LOG_LEVEL_WARNING, "Warning!");
APP_LOG(APP_LOG_LEVEL_ERROR, "Error occurred!");
```

## Application Lifecycle

```c
static void init(void) {
    srand(time(NULL));

    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
```

## Platform Detection

```c
#ifdef PBL_COLOR
    // Color display (basalt, chalk)
    graphics_context_set_fill_color(ctx, GColorRed);
#else
    // Black and white (aplite, diorite)
    graphics_context_set_fill_color(ctx, GColorWhite);
#endif

#ifdef PBL_ROUND
    // Round display (chalk)
#else
    // Rectangular display
#endif

#ifdef PBL_PLATFORM_APLITE
#endif
#ifdef PBL_PLATFORM_BASALT
#endif
#ifdef PBL_PLATFORM_CHALK
#endif
#ifdef PBL_PLATFORM_DIORITE
#endif
```
