# OBS Downstream Keyer

A plugin for OBS Studio that adds a broadcast-style downstream keyer (DSK) — a persistent overlay layer that sits on top of all your scenes and lets you punch individual graphics in and out independently during a live show.

Built by [Jomboy Media](https://jomboymedia.com).

---

## What It Does

You create one OBS scene that holds all your potential overlay graphics — sponsor logos, lower thirds, bugs, copyright notices, whatever you need. The plugin adds that scene as a layer on top of every other scene in your collection, then gives you a dock panel with a toggle button for each item.

Each item is completely independent. You can have Sponsor A live during segment one, bring in a lower third mid-conversation, then swap to Sponsor B later — all without touching your main scene.

---

## Installation

### Windows
1. Download `obs-downstream-keyer-windows-x64.zip` from the [latest release](../../releases/latest)
2. Extract and copy `obs-downstream-keyer.dll` to `C:\Program Files\obs-studio\obs-plugins\64bit\`
3. Restart OBS

### macOS
1. Download `obs-downstream-keyer-macos-universal.tar.xz` from the [latest release](../../releases/latest)
2. Extract and copy the `.plugin` bundle to `~/Library/Application Support/obs-studio/plugins/`
3. Restart OBS

### Linux
1. Download the `.deb` from the [latest release](../../releases/latest)
2. Run `sudo dpkg -i obs-downstream-keyer-*.deb`
3. Restart OBS

---

## Setup

1. **Create your DSK scene** — Make a new scene in OBS (e.g. `DSK Layer`) and add all the sources you might want to use as overlays. They'll start hidden by default.

2. **Open the dock** — Go to **Docks → Downstream Keyers**.

3. **Configure the plugin** — Click the **Settings** button in the dock, select your DSK scene from the dropdown, and click **Add DSK scene to all scenes**. This wires the overlay into your entire scene collection.

4. **Go live** — Toggle items on and off from the dock during your show. Each button is independent.

> If you add new scenes after initial setup, click **Add DSK scene to all scenes** again to include them.

---

## Features

### Dock Controls
Each source in your DSK scene gets its own toggle button in the dock. **Green always means live/on-air.** When a source is off, the button shows in its assigned color (default dark gray).

Use the toggle icon in the dock header to switch between **list view** (one row per item with a T button) and **grid view** (4-column compact grid, good for large collections).

### Color-Coded Buttons
Right-click any button and choose **Set Color…** to assign a custom color for when that source is off-air. Use this to visually identify items at a glance — red for lower thirds, blue for sponsor bugs, etc. The button always turns solid green when live regardless of its assigned color. Choose **Reset to Default Color** to go back to the standard dark gray.

### Drag-and-Drop Reordering
Click and hold any button then drag it to a new position. A green indicator line shows where it will land. Releasing drops it into place and the OBS scene item order is updated immediately. Works in both list and grid view. Note: reordering sources in OBS's Sources panel does not update the dock order — use the dock itself to reorder.

### Timer Progress Bar
When auto-hide is enabled on an item, the toggle button doubles as a visual countdown. The button starts fully green when activated and the color drains left to right back toward the item's assigned color over the configured duration. When time runs out the item is hidden automatically.

### Per-Item Transitions
Click the **T** button next to any item (or right-click in grid view) to configure show/hide transitions. All built-in OBS transitions are supported, plus any transition plugins you have installed — including **obs-move** for fly-ins, zooms, slides, and other motion effects.

When using obs-move, click **Configure…** after selecting it to set direction, easing, and distance using OBS's native properties panel.

### Auto-Hide Timer
In the **T** settings dialog, enable **Auto-hide** and set a duration in seconds. When the item is activated it will automatically deactivate after that time. The dock button shows a live countdown bar while it's running. Manually toggling the item off cancels the timer.

### Hotkeys
Every item in your DSK scene automatically gets a hotkey registered under **OBS Settings → Hotkeys** — look for entries starting with `DSK: Toggle`.

### Bitfocus Companion Integration
The plugin runs a local HTTP server (default port `4488`) for integration with [Bitfocus Companion](https://bitfocus.io/companion).

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/status` | Returns all items and their current state |
| `GET` | `/api/item/:name` | Returns state of a single item |
| `POST` | `/api/item/:name/activate` | Shows the item |
| `POST` | `/api/item/:name/deactivate` | Hides the item |
| `POST` | `/api/item/:name/toggle` | Toggles the item |

Source names in the URL must be URL-encoded. The port can be changed in Settings.

When an item has an active auto-hide countdown, the response includes `timeRemaining` (seconds, one decimal place):

```json
{"name":"Lower Third","active":true,"timeRemaining":4.2}
```

Items without a countdown omit the field entirely.

---

## Building From Source

Requires CMake 3.28+, a C++17 compiler, and an internet connection (the build system auto-downloads OBS and Qt dependencies).

**Windows**
```
cmake --preset windows-x64
cmake --build --preset windows-x64
```

**macOS**
```
cmake --preset macos
cmake --build --preset macos
```

**Linux**
```
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
```

---

## Requirements

- OBS Studio 28.0 or later
- Windows 10+, macOS 12+, or Ubuntu 22.04+
- [obs-move](https://obsproject.com/forum/resources/move-transition.913/) (optional, for motion transitions)
