---
name: pebble-watchface
description: Generate complete Pebble smartwatch watchfaces, build PBW artifacts, and test in QEMU emulator. Use when creating watchfaces, Pebble apps, animated displays, clock faces. Produces ready-to-install PBW files and runs them in emulator.
---

# Pebble Watchface Generator

Generate complete, buildable Pebble watchfaces with full PBW artifact output and QEMU testing.

## CRITICAL: End-to-End Delivery

This skill MUST produce a final `.pbw` file, test it, AND visually verify it looks correct.

Every watchface request follows this complete flow:

1. **Research** → [SUBAGENT] Gather requirements, study samples
2. **Design** → [SUBAGENT] Plan architecture and visuals
3. **Implement** → Write ALL project files
4. **Build** → Run `pebble build` to generate PBW
5. **Test** → Run in QEMU, capture screenshot, visually verify with Read tool
6. **Iterate** → Fix issues until screenshot looks good
7. **Deliver** → Report PBW location with verified screenshots

**Never stop until:**
- PBW is built successfully
- Screenshot is captured from QEMU
- Visual verification confirms it looks correct
- User receives the final artifact

---

## Phase 1: Research [USE SUBAGENT]

**Spawn a research subagent** using Task tool with `subagent_type: "Explore"` to:

### Gather Requirements
Ask the user (use AskUserQuestion if unclear):
- **Type**: Digital, analog, animated, or artistic?
- **Elements**: Time, date, battery, weather, custom graphics?
- **Animation**: Static, subtle, or complex animations?
- **Platforms**: All platforms or specific?

### Study Existing Code
The subagent should read and analyze:
```
samples/aqua-pbw/src/c/main.c
```

Key patterns to extract:
- Data structures for animated elements
- Animation loop structure
- Drawing functions
- Memory management patterns
- Battery-aware throttling

Also have subagent read relevant reference docs:
- `reference/pebble-api-reference.md`
- `reference/animation-patterns.md`
- `reference/drawing-guide.md`

**Subagent prompt example:**
```
Research Pebble watchface patterns for an animated [description] watchface.
Read samples/aqua-pbw/src/c/main.c and extract:
1. How animations are structured
2. Drawing patterns for [specific elements]
3. Memory management approach
4. Relevant API calls from reference/pebble-api-reference.md
Return a summary of patterns to use.
```

---

## Phase 2: Design [USE SUBAGENT]

**Spawn a planning subagent** using Task tool with `subagent_type: "Plan"` to:

### Create Design Specification
- Screen layout (144x168 rectangular, 180x180 round)
- Element positions and sizes
- Animation behavior and timing (intervals, speeds)
- Color scheme (with B&W fallback for aplite)
- Data structures needed
- Layer hierarchy

### CRITICAL: Layout Planning to Prevent Cropping
**You MUST calculate exact pixel positions to ensure nothing is cropped.**

For rectangular displays (144x168):
```
Available space: X: 0-143, Y: 0-167
Safe margins: 2-5 pixels from edges
```

For each visual element, calculate:
1. **Y position**: Where does it start vertically?
2. **Element height**: How tall is it?
3. **Bottom edge**: Y_position + height must be < 168 (with margin)

Example layout calculation for a beach scene:
```
SCREEN_HEIGHT = 168
Time text:     Y=5,  height=45  → bottom at 50  ✓
Sky:           Y=0,  height=50  → bottom at 50  ✓
Ocean:         Y=50, height=60  → bottom at 110 ✓
Sand:          Y=110, height=58 → bottom at 168 ✓
Castle:        Must fit within sand zone (Y=110-168)
  - Castle height: 40px
  - Castle Y: 168 - 40 - 5 = 123 (5px margin)
  - Castle top: 123, bottom: 163 ✓
```

**FAIL CONDITIONS to check in design:**
- Element bottom edge >= SCREEN_HEIGHT
- Element right edge >= SCREEN_WIDTH
- GPath points with negative offsets that extend beyond anchor point
- Elements positioned relative to SCREEN_HEIGHT without accounting for element size

### GPath Positioning Guide
GPaths use **relative coordinates from an anchor point**. Calculate carefully:

```c
// GPath points are RELATIVE to where you move_to
static GPoint castle_points[] = {
    {-35, 0},    // 35px LEFT of anchor, AT anchor Y
    {-35, -40},  // 35px left, 40px ABOVE anchor
    {35, 0},     // 35px RIGHT of anchor
};

// Anchor positioning calculation:
// If castle_points go from Y=0 to Y=-40 (40px tall, extending UP)
// And you want bottom of castle at Y=163 (5px margin from 168)
// Then anchor Y = 163 (the base of the castle)
gpath_move_to(castle_path, GPoint(SCREEN_WIDTH/2, 163));
```

**GPath bounds check:**
1. Find min/max X in points array → gives width range from anchor
2. Find min/max Y in points array → gives height range from anchor
3. Calculate: anchor_x + min_x >= 0, anchor_x + max_x < SCREEN_WIDTH
4. Calculate: anchor_y + min_y >= 0, anchor_y + max_y < SCREEN_HEIGHT

### Architecture Planning
- What structs are needed?
- How many animated elements?
- Update interval (50ms smooth, 100ms battery-saving)
- Collision detection needed?
- Memory pre-allocation strategy

**Subagent prompt example:**
```
Plan the implementation for a Pebble watchface with [description].
Based on the research findings, create:
1. Data structure definitions
2. Layer hierarchy
3. Animation timing strategy
4. Drawing function list
5. File structure
Return a detailed implementation plan.
```

---

## Phase 3: Implementation

**Do this directly** (not a subagent) - write all files:

### Create Project Directory
```bash
mkdir -p /path/to/watchface-name/src/c
mkdir -p /path/to/watchface-name/resources
```

### Write ALL Required Files

**1. package.json** (REQUIRED)
```json
{
  "name": "watchface-name",
  "author": "Author Name",
  "version": "1.0.0",
  "keywords": ["pebble-app"],
  "private": true,
  "dependencies": {},
  "pebble": {
    "displayName": "Watchface Display Name",
    "uuid": "GENERATE-NEW-UUID-HERE",
    "sdkVersion": "3",
    "enableMultiJS": false,
    "targetPlatforms": ["aplite", "basalt", "chalk", "diorite"],
    "watchapp": { "watchface": true },
    "resources": { "media": [] }
  }
}
```
Generate UUID: `python3 -c "import uuid; print(uuid.uuid4())"`

**2. wscript** (REQUIRED)
```python
top = '.'
out = 'build'

def options(ctx):
    ctx.load('pebble_sdk')

def configure(ctx):
    ctx.load('pebble_sdk')

def build(ctx):
    ctx.load('pebble_sdk')
    binaries = []
    for p in ctx.env.TARGET_PLATFORMS:
        ctx.set_env(ctx.all_envs[p])
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_program(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf)
        binaries.append({'platform': p, 'app_elf': app_elf})
    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries, js=ctx.path.ant_glob(['src/js/**/*.js']))
```

**3. src/c/main.c** (REQUIRED)
Write complete watchface code following the design from Phase 2.

Use templates as starting points:
- [templates/animated-watchface.c](templates/animated-watchface.c)
- [templates/static-watchface.c](templates/static-watchface.c)
- [templates/rocky-watchface.js](templates/rocky-watchface.js)

### Code Requirements
- `#include <pebble.h>`
- Implement `main()`, `init()`, `deinit()`
- Window with load/unload handlers
- `tick_timer_service_subscribe()` for time updates
- For animations: `app_timer_register()` with 50ms interval
- Pre-allocate GPath in window_load
- Destroy all resources in unload handlers
- Fixed-point math only (sin_lookup/cos_lookup)

---

## Phase 4: Build PBW

### Run the Build
```bash
cd /path/to/watchface-name
pebble build
```

### Verify Build Success
```bash
ls -la build/*.pbw
```

Expected output: `build/watchface-name.pbw`

### Handle Build Errors
If build fails:
1. Read error message
2. Fix C code (syntax, types, missing includes)
3. Run `pebble build` again
4. Repeat until successful

---

## Phase 5: Test in QEMU Emulator

**REQUIRED** - Must test AND visually verify before delivering.

### Step 1: Launch Emulator and Install
```bash
# Primary test - color rectangular (Pebble Time)
pebble install --emulator basalt
```

Wait a few seconds for the watchface to load and render.

### Step 2: Capture Screenshot (MANDATORY)
```bash
pebble screenshot --emulator basalt screenshot_basalt.png
```

This saves the screenshot to the current directory.

### Step 3: Visual Verification (MANDATORY)

**Use the Read tool to view the screenshot image:**
```
Read tool: screenshot_basalt.png
```

**CRITICAL: Perform thorough visual verification using this detailed checklist.**

#### A. Cropping Check (FAIL if any element is cut off)
- [ ] **All visual elements fully visible** - No element should be cut off at screen edges
- [ ] **Key graphics not clipped** - Main visual elements (characters, objects, icons) must be 100% within screen bounds
- [ ] **No overflow at bottom** - Elements near y=168 (rect) or near circular edge (round) must have margin
- [ ] **No overflow at sides** - Elements near x=0 or x=144 must have margin
- [ ] **Text not truncated** - All text fits within its designated area

#### B. Positioning Check (FAIL if layout doesn't match design)
- [ ] **Time in correct position** - Matches the designed location (top, center, bottom, etc.)
- [ ] **Visual elements properly placed** - Each element appears where the design specified
- [ ] **Correct vertical zones** - If design has zones (sky/ocean/sand), verify Y coordinates match
- [ ] **Proportional spacing** - Elements have appropriate margins and don't overlap incorrectly
- [ ] **Center alignment** - Centered elements are actually centered on screen

#### C. Color Scheme Check (FAIL if colors don't match design)
- [ ] **Primary colors correct** - Main background/foreground colors match design spec
- [ ] **Accent colors correct** - Secondary/highlight colors match design spec
- [ ] **B&W fallback works** (aplite) - Grayscale version has good contrast and readability
- [ ] **No color bleeding** - Adjacent colors don't bleed into each other
- [ ] **Contrast sufficient** - Text and elements are readable against their backgrounds

#### D. Element Visibility Check (FAIL if key elements missing/invisible)
- [ ] **All designed elements present** - Every element from the design is rendered
- [ ] **Elements appropriately sized** - Not too small to see or too large for the screen
- [ ] **Z-order correct** - Foreground elements appear in front of background
- [ ] **No invisible elements** - Elements aren't hidden behind others or same color as background

#### E. Design Intent Check (FAIL if doesn't match user request)
- [ ] **Theme recognizable** - The watchface clearly represents the requested theme
- [ ] **Key features prominent** - Main visual features are visible and not obscured
- [ ] **Animation state reasonable** - Animated elements captured in a representative state
- [ ] **Overall composition balanced** - Layout looks intentional, not accidental

**STOP AND FIX if ANY check fails.** Do not proceed to delivery with visual issues.

### Step 4: Fix Issues and Re-test

If visual verification fails, identify the issue category and apply the appropriate fix:

#### Fixing Cropping Issues
- **Bottom cropping**: Reduce Y coordinates of elements, or reduce element height
- **Side cropping**: Reduce X coordinates or width, ensure `SCREEN_WIDTH/2` centering
- **Common mistake**: Placing elements at Y positions that don't account for element height
- **Fix formula**: If element height is H and you want it at bottom, use `Y = SCREEN_HEIGHT - H - margin`

#### Fixing Positioning Issues
- **Wrong zone**: Recalculate Y constants (e.g., `SKY_HEIGHT`, `OCEAN_TOP`, `SAND_TOP`)
- **Off-center**: Use `SCREEN_WIDTH / 2 - element_width / 2` for X position
- **Overlapping**: Ensure zone boundaries don't overlap (previous zone end < next zone start)

#### Fixing Color Issues
- **Wrong color**: Check `#ifdef PBL_COLOR` blocks define correct GColor values
- **B&W contrast**: Use `GColorWhite`/`GColorBlack`/`GColorLightGray` with high contrast
- **Invisible element**: Ensure element color differs from background color

#### Fixing Element Visibility Issues
- **Element too small**: Increase dimensions (radius, width, height, point coordinates)
- **Element missing**: Check if draw function is called, check NULL pointers
- **Wrong z-order**: Reorder draw calls (painter's algorithm: back to front)

**Iteration Process:**
1. Identify which check(s) failed from Step 3
2. Apply the specific fix from above
3. Rebuild: `pebble build`
4. Reinstall: `pebble install --emulator basalt`
5. New screenshot: `pebble screenshot --emulator basalt screenshot_basalt.png`
6. Re-verify with Read tool using the FULL checklist
7. **Repeat until ALL checks pass**

**Do not skip any checks.** A watchface with cropping issues is not acceptable for delivery.

### Step 5: Test Other Platforms

After basalt looks good, test B&W and round:
```bash
# B&W test
pebble install --emulator aplite
pebble screenshot --emulator aplite screenshot_aplite.png
# Use Read tool to verify screenshot_aplite.png

# Round display test
pebble install --emulator chalk
pebble screenshot --emulator chalk screenshot_chalk.png
# Use Read tool to verify screenshot_chalk.png
```

### Step 6: Check Logs for Errors
```bash
pebble logs --emulator basalt
```

Look for:
- APP_LOG errors
- Crashes or exceptions
- Memory warnings

### Emulator Platforms Reference
| Platform | Display | Resolution | Priority |
|----------|---------|------------|----------|
| basalt   | Color, Rect | 144x168 | Test first |
| aplite   | B&W, Rect | 144x168 | Compatibility |
| chalk    | Color, Round | 180x180 | Round layout |
| diorite  | Color, Rect | 144x168 | Optional |

---

## Phase 6: Deliver

### Report to User
After successful build AND visual verification:

1. **PBW Location**: `build/watchface-name.pbw`

2. **Verified Screenshots**: Show the user the captured screenshots
   - `screenshot_basalt.png` - Main color display
   - `screenshot_aplite.png` - B&W compatibility (if tested)
   - `screenshot_chalk.png` - Round display (if tested)

3. **Visual Confirmation**: Describe what the watchface shows:
   - Time display style and position
   - Animated elements (if any)
   - Color scheme
   - Any special features

4. **Install Commands**:
   - Emulator: `pebble install --emulator basalt`
   - Device: `pebble install --cloudpebble`

5. **Summary**: Brief description of what was built and verified

---

## Subagent Summary

| Phase | Subagent Type | Purpose |
|-------|---------------|---------|
| Research | `Explore` | Read samples, extract patterns |
| Design | `Plan` | Create implementation plan |
| Implement | Direct | Write all project files |
| Build | Direct | Run `pebble build` |
| Test | Direct | Run in QEMU, screenshot, verify with Read tool |
| Iterate | Direct | Fix code until screenshot looks correct |
| Deliver | Direct | Report PBW + screenshots to user |

---

## Quick Reference

### Screen Dimensions
| Platform | Resolution | Shape | Color |
|----------|------------|-------|-------|
| aplite   | 144x168    | Rect  | B&W   |
| basalt   | 144x168    | Rect  | 64-color |
| chalk    | 180x180    | Round | 64-color |
| diorite  | 144x168    | Rect  | 64-color |

### Key APIs
```c
// Drawing
graphics_fill_circle(ctx, center, radius);
graphics_draw_line(ctx, start, end);
graphics_fill_rect(ctx, rect, corner_radius, corners);

// Fixed-point trig (NO FLOATS!)
sin_lookup(angle);  // 0 to TRIG_MAX_ANGLE (65536)
cos_lookup(angle);  // returns -TRIG_MAX_RATIO to +TRIG_MAX_RATIO

// Time
time_t temp = time(NULL);
struct tm *tick_time = localtime(&temp);
tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

// Animation
app_timer_register(50, callback, NULL);  // 50ms = 20 FPS
layer_mark_dirty(layer);
```

### Build & Test Commands
```bash
pebble build                        # Build PBW
pebble install --emulator basalt    # Test in QEMU
pebble logs --emulator basalt       # View logs
pebble screenshot --emulator basalt # Capture screen
pebble install --cloudpebble        # Deploy to device
```

---

## Constraints

1. **No Floating Point** - Use sin_lookup/cos_lookup only
2. **Pre-allocate Memory** - Create GPath in window_load
3. **Battery Aware** - Throttle when battery < 20%
4. **Clean Resources** - Destroy in unload handlers
5. **NULL Checks** - Verify pointers before use
6. **Overflow Protection** - Use modulo on counters

---

## File Checklist

Before building:
- [ ] `package.json` with valid UUID
- [ ] `wscript` with build config
- [ ] `src/c/main.c` with complete code
- [ ] `resources/` directory exists

Build: `pebble build`
Test: `pebble install --emulator basalt`
Output: `build/[name].pbw`
