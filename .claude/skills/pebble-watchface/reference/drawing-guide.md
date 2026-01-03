# Pebble Drawing Guide

## Screen Coordinates

```
(0,0) ────────────────────→ X (144 for rect, 180 for round)
  │
  │      ┌─────────────┐
  │      │             │
  │      │   Screen    │
  │      │             │
  │      │             │
  │      └─────────────┘
  ↓
  Y (168 for rect, 180 for round)
```

## Basic Shapes

### Circles
```c
// Filled circle
graphics_context_set_fill_color(ctx, GColorWhite);
graphics_fill_circle(ctx, GPoint(72, 84), 20);

// Outlined circle
graphics_context_set_stroke_color(ctx, GColorWhite);
graphics_draw_circle(ctx, GPoint(72, 84), 20);
```

### Rectangles
```c
// Define rectangle
GRect rect = GRect(10, 10, 50, 30);  // x, y, width, height

// Or using struct initialization
GRect rect = {
    .origin = {10, 10},
    .size = {50, 30}
};

// Filled rectangle
graphics_fill_rect(ctx, rect, 0, GCornerNone);  // Sharp corners

// Rounded rectangle
graphics_fill_rect(ctx, rect, 5, GCornersAll);  // 5px corner radius

// Only round specific corners
graphics_fill_rect(ctx, rect, 5, GCornersTop);
graphics_fill_rect(ctx, rect, 5, GCornerTopLeft | GCornerBottomRight);

// Outlined rectangle
graphics_draw_rect(ctx, rect);
```

### Lines
```c
graphics_context_set_stroke_color(ctx, GColorWhite);
graphics_context_set_stroke_width(ctx, 2);

GPoint start = GPoint(10, 10);
GPoint end = GPoint(100, 100);

graphics_draw_line(ctx, start, end);
```

### Pixels
```c
graphics_context_set_stroke_color(ctx, GColorWhite);
graphics_draw_pixel(ctx, GPoint(72, 84));
```

## Paths (Complex Shapes)

### Triangle
```c
static GPoint triangle_points[3] = {
    {72, 20},   // Top
    {40, 100},  // Bottom left
    {104, 100}  // Bottom right
};

static GPathInfo triangle_info = {
    .num_points = 3,
    .points = triangle_points
};

// Create once in window_load
static GPath *triangle = NULL;
triangle = gpath_create(&triangle_info);

// Draw in update_proc
graphics_context_set_fill_color(ctx, GColorWhite);
gpath_draw_filled(ctx, triangle);

// Destroy in window_unload
gpath_destroy(triangle);
```

### Arrow/Chevron
```c
static GPoint arrow_points[7] = {
    {0, 10},    // Left base
    {15, 10},   // Left inner
    {15, 0},    // Top inner
    {25, 15},   // Point
    {15, 30},   // Bottom inner
    {15, 20},   // Right inner
    {0, 20}     // Right base
};
```

### Star
```c
// 5-point star
static GPoint star_points[10];

static void init_star(GPoint center, int outer_r, int inner_r) {
    for (int i = 0; i < 10; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE / 10) - (TRIG_MAX_ANGLE / 4);
        int radius = (i % 2 == 0) ? outer_r : inner_r;

        star_points[i].x = center.x + (sin_lookup(angle) * radius) / TRIG_MAX_RATIO;
        star_points[i].y = center.y + (cos_lookup(angle) * radius) / TRIG_MAX_RATIO;
    }
}
```

## Drawing Arcs

Pebble doesn't have a native arc function, but you can draw arcs with lines:

```c
static void draw_arc(GContext *ctx, GPoint center, int radius,
                     int32_t start_angle, int32_t end_angle) {
    int segments = 20;
    int32_t angle_step = (end_angle - start_angle) / segments;

    GPoint prev;
    prev.x = center.x + (sin_lookup(start_angle) * radius) / TRIG_MAX_RATIO;
    prev.y = center.y - (cos_lookup(start_angle) * radius) / TRIG_MAX_RATIO;

    for (int i = 1; i <= segments; i++) {
        int32_t angle = start_angle + (i * angle_step);
        GPoint curr;
        curr.x = center.x + (sin_lookup(angle) * radius) / TRIG_MAX_RATIO;
        curr.y = center.y - (cos_lookup(angle) * radius) / TRIG_MAX_RATIO;

        graphics_draw_line(ctx, prev, curr);
        prev = curr;
    }
}
```

## Text Rendering

### Using Text Layers
```c
static TextLayer *s_time_layer;

// Create
s_time_layer = text_layer_create(GRect(0, 50, 144, 40));
text_layer_set_background_color(s_time_layer, GColorClear);
text_layer_set_text_color(s_time_layer, GColorWhite);
text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

// Update text
text_layer_set_text(s_time_layer, "12:34");

// Destroy
text_layer_destroy(s_time_layer);
```

### Drawing Text Directly
```c
static void draw_text(GContext *ctx, const char *text, GRect bounds) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx,
                       text,
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       bounds,
                       GTextOverflowModeWordWrap,
                       GTextAlignmentCenter,
                       NULL);
}
```

## Color Management

### Platform-Aware Colors
```c
#ifdef PBL_COLOR
    #define BACKGROUND_COLOR GColorDarkGray
    #define FOREGROUND_COLOR GColorCyan
    #define ACCENT_COLOR GColorRed
#else
    #define BACKGROUND_COLOR GColorBlack
    #define FOREGROUND_COLOR GColorWhite
    #define ACCENT_COLOR GColorWhite
#endif
```

### Color Palette for Basalt/Chalk
```c
// Primary colors
GColorRed, GColorGreen, GColorBlue

// Warm colors
GColorOrange, GColorYellow, GColorRajah, GColorMelon

// Cool colors
GColorCyan, GColorTiffanyBlue, GColorCadetBlue, GColorPictonBlue

// Purples/Pinks
GColorMagenta, GColorPurple, GColorVividViolet, GColorShockingPink

// Neutrals
GColorBlack, GColorOxfordBlue, GColorDarkGray, GColorLightGray, GColorWhite

// Create from RGB (each component 0-3 for 64-color palette)
GColor my_color = GColorFromRGB(255, 128, 0);  // Orange
```

## Drawing Order (Painter's Algorithm)

Draw from back to front:

```c
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    // 1. Clear background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // 2. Draw background elements
    draw_background(ctx);

    // 3. Draw middle-ground elements
    draw_scenery(ctx);

    // 4. Draw foreground elements
    draw_characters(ctx);

    // 5. Draw UI overlays last
    draw_ui(ctx);
}
```

## Common Drawing Patterns

### Battery Bar
```c
static void draw_battery_bar(GContext *ctx, GRect bounds, int percent) {
    // Outline
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, bounds);

    // Fill
    graphics_context_set_fill_color(ctx, GColorWhite);
    int fill_width = (bounds.size.w * percent) / 100;
    GRect fill = {bounds.origin, {fill_width, bounds.size.h}};
    graphics_fill_rect(ctx, fill, 0, GCornerNone);

    // Battery tip
    GRect tip = {
        {bounds.origin.x + bounds.size.w, bounds.origin.y + 2},
        {2, bounds.size.h - 4}
    };
    graphics_fill_rect(ctx, tip, 0, GCornerNone);
}
```

### Analog Clock Face
```c
static void draw_clock_face(GContext *ctx, GPoint center, int radius) {
    // Clock circle
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, center, radius);

    // Hour markers
    for (int i = 0; i < 12; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE) / 12;

        int inner_r = radius - 8;
        int outer_r = radius - 2;

        GPoint inner = {
            center.x + (sin_lookup(angle) * inner_r) / TRIG_MAX_RATIO,
            center.y - (cos_lookup(angle) * inner_r) / TRIG_MAX_RATIO
        };
        GPoint outer = {
            center.x + (sin_lookup(angle) * outer_r) / TRIG_MAX_RATIO,
            center.y - (cos_lookup(angle) * outer_r) / TRIG_MAX_RATIO
        };

        graphics_draw_line(ctx, inner, outer);
    }
}
```

### Gradient Effect (Dithered)
```c
// Pebble doesn't support true gradients, but you can dither
static void draw_dithered_gradient(GContext *ctx, GRect bounds,
                                   GColor color1, GColor color2) {
    for (int y = bounds.origin.y; y < bounds.origin.y + bounds.size.h; y++) {
        for (int x = bounds.origin.x; x < bounds.origin.x + bounds.size.w; x++) {
            // Checkerboard dither pattern
            bool use_color1 = ((x + y) % 2 == 0);

            // Bias based on position
            int threshold = (y - bounds.origin.y) * 100 / bounds.size.h;
            if (random_in_range(0, 100) > threshold) {
                use_color1 = true;
            }

            graphics_context_set_stroke_color(ctx, use_color1 ? color1 : color2);
            graphics_draw_pixel(ctx, GPoint(x, y));
        }
    }
}
```

## Performance Tips

1. **Minimize draw calls**: Batch similar operations
2. **Pre-calculate positions**: Don't do math in draw functions if it can be done in update
3. **Use layer_mark_dirty()**: Only redraw when necessary
4. **Clip to visible area**: Skip drawing objects outside screen bounds
5. **Use appropriate stroke widths**: Thicker lines are faster than thin

```c
// Check if point is on screen before drawing
static bool is_visible(GPoint p, int margin) {
    return p.x >= -margin && p.x <= 144 + margin &&
           p.y >= -margin && p.y <= 168 + margin;
}
```
