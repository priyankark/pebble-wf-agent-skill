#!/usr/bin/env python3
"""
Create app icons from screenshot_basalt.png

Usage:
    python3 create_app_icons.py [project_dir]

If project_dir is not specified, uses current directory.

Creates:
    - icon_80x80.png (small app icon)
    - icon_144x144.png (large app icon)
"""

import sys
import os
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow is required. Install with: pip3 install Pillow")
    sys.exit(1)


def create_app_icons(project_dir: str = "."):
    """Create 80x80 and 144x144 app icons from screenshot_basalt.png"""

    project_path = Path(project_dir)
    screenshot_path = project_path / "screenshot_basalt.png"

    if not screenshot_path.exists():
        print(f"Error: {screenshot_path} not found")
        print("Run 'pebble screenshot --emulator basalt screenshot_basalt.png' first")
        sys.exit(1)

    # Load the screenshot
    img = Image.open(screenshot_path)

    # Create 80x80 icon
    icon_80 = img.resize((80, 80), Image.Resampling.LANCZOS)
    icon_80_path = project_path / "icon_80x80.png"
    icon_80.save(icon_80_path)
    print(f"Created: {icon_80_path}")

    # Create 144x144 icon
    icon_144 = img.resize((144, 144), Image.Resampling.LANCZOS)
    icon_144_path = project_path / "icon_144x144.png"
    icon_144.save(icon_144_path)
    print(f"Created: {icon_144_path}")

    print("\nApp icons created successfully!")


if __name__ == "__main__":
    project_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    create_app_icons(project_dir)
