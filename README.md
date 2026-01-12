# Pebble Watchface Generator Skill

Generate complete, buildable Pebble smartwatch watchfaces using Claude Code with full PBW artifact output and QEMU emulator testing.

## What This Does

This repository contains a **Claude Code skill** that transforms natural language descriptions into fully functional Pebble watchfaces. Simply describe what you want, and Claude will:

1. **Design** the watchface architecture
2. **Generate** all source files (C code, package.json, wscript)
3. **Build** a ready-to-install `.pbw` file
4. **Test** in the QEMU emulator
5. **Verify** visually with screenshots
6. **Deliver** the final artifacts with app icons and animated previews

## Sample Watchfaces

These watchfaces were generated using this agent skill and are live on the Rebble App Store:

- [The Swordsmen](https://store-beta.rebble.io/app/6959f7d4828bd90009ec53a8) - Animated Prince of Persia sword fighting scene
- [Castle Knights](https://store-beta.rebble.io/app/6959302c39d92d0009244929) - Animated medieval castle with jousting knights

## Prerequisites

Before using this skill, ensure you have:

- **Claude Code CLI** installed and configured
- **Pebble SDK** Follow the instructions in the official [documentation](https://developer.repebble.com/sdk/)
- **QEMU** for emulator testing (bundled with Pebble SDK)
- **Python 3** with Pillow (`pip install Pillow`) for icon/GIF generation

## Quick Start

### 1. Clone This Repository

```bash
git clone <repo-url>
cd pebble-wf-skill
```

### 2. Run Claude Code

```bash
claude
```

### 3. Ask for a Watchface

Simply describe what you want:

```
Create a retro digital watchface with a neon green display on black background
```

Or:

```
Make an animated watchface with floating bubbles and the time in the center
```

Claude will automatically invoke the `pebble-watchface` skill and handle everything from design to delivery.

## How the Skill Works

### Skill Location

```
.claude/skills/pebble-watchface/
├── SKILL.md              # Main skill definition
├── reference/            # API documentation
│   ├── pebble-api-reference.md
│   ├── animation-patterns.md
│   └── drawing-guide.md
├── samples/              # Working example watchfaces
│   └── aqua-pbw/         # Animated aquarium watchface
├── scripts/              # Helper utilities
│   ├── create_app_icons.py
│   ├── create_preview_gif.py
│   ├── create_project.py
│   ├── generate_uuid.py
│   └── validate_project.py
└── templates/            # Code templates
    ├── animated-watchface.c
    ├── static-watchface.c
    ├── rocky-watchface.js
    ├── package.json.template
    └── wscript.template
```

### The 7-Phase Workflow

The skill follows a rigorous end-to-end workflow:

| Phase | What Happens |
|-------|--------------|
| **1. Research** | Gathers requirements, studies sample code patterns |
| **2. Design** | Plans layout, animations, data structures |
| **3. Implement** | Writes all project files (main.c, package.json, wscript) |
| **4. Build** | Runs `pebble build` to generate the PBW |
| **5. Test** | Installs in QEMU, captures screenshots |
| **6. Iterate** | Fixes issues until visual verification passes |
| **7. Deliver** | Reports PBW location with icons and preview GIFs |

### Supported Platforms

| Platform | Display | Resolution | Colors |
|----------|---------|------------|--------|
| aplite   | Rectangular | 144x168 | Black & White |
| basalt   | Rectangular | 144x168 | 64 colors |
| chalk    | Round | 180x180 | 64 colors |
| diorite  | Rectangular | 144x168 | 64 colors |

## Example Prompts

### Simple Digital Watchface
```
Create a minimalist digital watchface with large white numbers on a dark blue background
```

### Analog with Complications
```
Make an analog watchface with hour and minute hands, a small seconds subdial,
and battery indicator in the top right
```

### Animated Watchface
```
Create a beach-themed watchface with animated waves, a sandcastle, and the time
displayed in the sky area
```

### Retro Gaming Style
```
Design a watchface that looks like an old Game Boy screen with pixelated time display
```

## Output Artifacts

After successful generation, you'll receive:

```
your-watchface/
├── build/
│   └── your-watchface.pbw    # Ready-to-install watchface
├── src/c/
│   └── main.c                # Generated C source code
├── package.json              # Pebble project manifest
├── wscript                   # Build configuration
├── screenshot_basalt.png     # Color display preview
├── screenshot_aplite.png     # B&W display preview
├── screenshot_chalk.png      # Round display preview
├── icon_80x80.png            # Small app icon
├── icon_144x144.png          # Large app icon
├── preview_basalt.gif        # Animated color preview
├── preview_aplite.gif        # Animated B&W preview
└── preview_chalk.gif         # Animated round preview
```

## Installing Your Watchface

### On Emulator
```bash
cd your-watchface
pebble install --emulator basalt
```

### On Physical Watch
```bash
pebble install --cloudpebble
```

Or transfer the `.pbw` file to your phone and open with the Pebble app.

## Customizing the Skill

### Adding New Templates

Add new C templates to `.claude/skills/pebble-watchface/templates/`:

```c
// templates/my-custom-template.c
#include <pebble.h>

// Your template code here...
```

Reference it in SKILL.md under the templates section.

### Adding Reference Documentation

Add new reference docs to `.claude/skills/pebble-watchface/reference/`:

```markdown
# My Custom Reference

Document specific patterns, APIs, or techniques here.
```

### Adding Sample Watchfaces

Add working examples to `.claude/skills/pebble-watchface/samples/`:

```
samples/
└── my-example/
    ├── src/c/main.c
    ├── package.json
    ├── wscript
    └── resources/
```

## Key Technical Constraints

The Pebble platform has specific requirements that the skill handles automatically:

1. **No Floating Point** - Uses `sin_lookup()`/`cos_lookup()` for trigonometry
2. **Pre-allocated Memory** - Creates GPaths in `window_load`
3. **Fixed-Point Math** - TRIG_MAX_ANGLE = 65536 for full circle
4. **Battery Awareness** - Throttles animations when battery < 20%
5. **Resource Cleanup** - Properly destroys all resources in unload handlers

## Troubleshooting

### Build Fails
- Check for syntax errors in the generated C code
- Verify `pebble-sdk` is properly installed
- Ensure all required files exist (package.json, wscript, src/c/main.c)

### Emulator Won't Start
- Run `pebble sdk install-emulator basalt` to install emulator
- Check QEMU is installed: `which qemu-system-arm`

### Watchface Looks Wrong
- The skill includes visual verification - it will iterate until correct
- If issues persist, provide specific feedback about what's wrong

### Missing Dependencies
```bash
pip install pebble-sdk Pillow
```

## How Claude Code Skills Work

Claude Code skills are markdown files that provide domain-specific instructions. When you place a skill in `.claude/skills/`, Claude automatically:

1. **Detects** when the skill is relevant based on your request
2. **Loads** the skill instructions and reference materials
3. **Follows** the defined workflow phases
4. **Uses** provided templates and samples as references
5. **Executes** build and test commands
6. **Delivers** verified artifacts

### Skill File Structure

```markdown
---
name: skill-name
description: When to use this skill
---

# Skill Title

Instructions for Claude to follow...

## Phase 1: Research
...

## Phase 2: Implementation
...
```

The `SKILL.md` file is the entry point. Supporting files in `reference/`, `samples/`, and `templates/` provide additional context.

## Contributing

To improve this skill:

1. Test with various watchface descriptions
2. Add new templates for common patterns
3. Expand reference documentation
4. Add more sample watchfaces
5. Improve error handling in scripts

## Resources

- [Rebble Developer Portal](https://developer.rebble.io/)
- [Pebble SDK Documentation](https://developer.rebble.io/developer.pebble.com/docs/index.html)
- [Claude Code Documentation](https://docs.anthropic.com/claude-code)

## License

This skill and associated templates are provided for creating Pebble watchfaces. Individual watchfaces you create are your own.
