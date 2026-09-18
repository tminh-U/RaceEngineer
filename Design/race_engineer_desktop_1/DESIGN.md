---
name: Race Engineer Desktop
colors:
  surface: '#131315'
  surface-dim: '#131315'
  surface-bright: '#39393b'
  surface-container-lowest: '#0e0e10'
  surface-container-low: '#1b1b1d'
  surface-container: '#1f1f21'
  surface-container-high: '#2a2a2c'
  surface-container-highest: '#353437'
  on-surface: '#e4e2e4'
  on-surface-variant: '#c0c6d6'
  inverse-surface: '#e4e2e4'
  inverse-on-surface: '#303032'
  outline: '#8b91a0'
  outline-variant: '#414754'
  surface-tint: '#aac7ff'
  primary: '#aac7ff'
  on-primary: '#003064'
  primary-container: '#3e90ff'
  on-primary-container: '#002957'
  inverse-primary: '#005db8'
  secondary: '#47e266'
  on-secondary: '#003910'
  secondary-container: '#09bf49'
  on-secondary-container: '#004615'
  tertiary: '#ffb868'
  on-tertiary: '#482900'
  tertiary-container: '#ce7f00'
  on-tertiary-container: '#3f2300'
  error: '#ffb4ab'
  on-error: '#690005'
  error-container: '#93000a'
  on-error-container: '#ffdad6'
  primary-fixed: '#d6e3ff'
  primary-fixed-dim: '#aac7ff'
  on-primary-fixed: '#001b3e'
  on-primary-fixed-variant: '#00468d'
  secondary-fixed: '#6cff82'
  secondary-fixed-dim: '#47e266'
  on-secondary-fixed: '#002106'
  on-secondary-fixed-variant: '#00531a'
  tertiary-fixed: '#ffddbb'
  tertiary-fixed-dim: '#ffb868'
  on-tertiary-fixed: '#2b1700'
  on-tertiary-fixed-variant: '#673d00'
  background: '#131315'
  on-background: '#e4e2e4'
  surface-variant: '#353437'
typography:
  display-lg:
    fontFamily: Inter
    fontSize: 28px
    fontWeight: '600'
    lineHeight: 34px
    letterSpacing: -0.02em
  headline-lg:
    fontFamily: Inter
    fontSize: 20px
    fontWeight: '600'
    lineHeight: 26px
    letterSpacing: -0.015em
  headline-sm:
    fontFamily: Inter
    fontSize: 15px
    fontWeight: '600'
    lineHeight: 20px
    letterSpacing: -0.01em
  body-lg:
    fontFamily: Inter
    fontSize: 14px
    fontWeight: '400'
    lineHeight: 20px
    letterSpacing: -0.005em
  body-sm:
    fontFamily: Inter
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
    letterSpacing: 0em
  telemetry-display:
    fontFamily: JetBrains Mono
    fontSize: 22px
    fontWeight: '500'
    lineHeight: 26px
    letterSpacing: -0.02em
  telemetry-body:
    fontFamily: JetBrains Mono
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
    letterSpacing: -0.01em
  label-md:
    fontFamily: Inter
    fontSize: 12px
    fontWeight: '500'
    lineHeight: 16px
    letterSpacing: 0.02em
  label-sm:
    fontFamily: Inter
    fontSize: 11px
    fontWeight: '500'
    lineHeight: 14px
    letterSpacing: 0.04em
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  gutter: 1rem
  gutter-compact: 0.5rem
  margin: 1.25rem
  margin-window: 0.75rem
  space-xs: 0.25rem
  space-sm: 0.5rem
  space-md: 0.75rem
  space-lg: 1rem
  space-xl: 1.5rem
---

## Brand & Style
The design system establishes a high-precision, focused motorsport environment calibrated for instantaneous decision-making during competitive sessions. Drawing inspiration from macOS system architecture, professional audio workstations, and Apple Fitness metrics, the interface eliminates the typical aggressive neon aesthetics of motorsport gaming software in favor of institutional clarity, silent competence, and deep ergonomic comfort.

Key attributes:
- **Atmospheric & Subdued:** Deep neutral charcoals isolate critical track data without blinding the driver or engineer in low-light simulator rigs.
- **Instrumental Precision:** Information is weighted with strict typography, native macOS control paradigms, and legible telemetry readouts.
- **Calm Authority:** Feedback is communicative rather than sensory overload. Voice synthesis, waveform metrics, and automated strategy inputs integrate seamlessly via fluid, translucent surfaces.

## Colors
The palette is built on strict chromatic discipline. Deep, neutral charcoal foundations prevent eye fatigue while structural accents and dynamic semantic states ensure mission-critical clarity.

- **Canvas & Surface Base:** `#161618` (Window canvas base), `#1C1C1E` (Primary surface / inset table background), `#242428` (Elevated secondary surface / cards).
- **Subtle Overlays & Dividers:** `rgba(255, 255, 255, 0.06)` for interactive row highlights; `rgba(255, 255, 255, 0.08)` for structural hairpins and divider borders.
- **Key Accent:** `#0A84FF` serves as the primary system driver for focus states, active segmented items, and voice transmission highlights.
- **Telemetry & Status Metrics:**
  - **Healthy / Optimal:** `#30D158` (Tire temps in green zone, telemetry link active, delta positive).
  - **Cautionary / Warning:** `#FF9F0A` (Brake wear, fuel reserve alert, dynamic track state shift).
  - **Critical / Danger:** `#FF453A` (Engine failure risk, pit limiter violation, rapid pressure drop).
- **Text & Glyph Hierarchy:**
  - **Primary:** `#FFFFFF` (100% white for titles, numerical deltas, critical telemetry readouts).
  - **Secondary:** `#8E8E93` (Muted labels, units of measure, timestamps).
  - **Tertiary:** `rgba(142, 142, 147, 0.60)` for passive telemetry graph grids and inactive switches.

## Typography
The system employs `Inter` for interface structure and conversational messaging, paired with `JetBrains Mono` for all tabular telemetry readouts, lap times, delta splits, and mechanical parameters.

- **Tabular Numerals Everywhere in Telemetry:** All numeric telemetry, strategy timings, and sensor readouts must use strict monospacing (`JetBrains Mono` or `font-variant-numeric: tabular-nums`) to prevent layout jitter during rapid multi-hertz data streams.
- **Section Headers & Settings:** Inset group headers use `label-sm` in all-caps uppercase with extended letter spacing (`0.04em`) styled in muted neutral `#8E8E93`.
- **System Prompts & Assistant Text:** Engineer voice interactions display in `body-lg` with a high contrast ratio to stand out instantly over background status panels.

## Layout & Spacing
The layout adheres to a structured desktop application architecture containing three core operational zones: macOS unified titlebar and toolbar, a primary telemetry and track map viewport, and an inset grouped inspector panel for engineering parameters and AI dialogue.

- **Window Layout Discipline:**
  - Desktop standard layout spans a fixed sidebar navigation (240px width), a responsive central dashboard canvas, and a right-anchored collapsible engineer strip (320px width).
  - `margin-window` (0.75rem / 12px) provides inner separation between macOS native frame borders and secondary panels.
  - Inset tables and component blocks use `space-md` (12px) internal padding and `space-xs` (4px) to `space-sm` (8px) gaps between related sub-elements.
- **Reflow & Responsive Adaptations:**
  - **Full Desktop (>1440px):** 3-column expanded layout (Telemetry View, Strategy Flow & Map, Live AI Transcript & Inset Tuning).
  - **Compact Desktop (1024px - 1439px):** Inspector defaults to an overlay flyout; unified telemetry and audio wave strip consolidate into a persistent top toolbar.
  - **Minimum Supported Window (800x600px):** Sidebar collapses to an icon rail (48px); inset grouped cards stack in a single vertically scrollable column.

## Elevation & Depth
Depth is created through macOS-inspired frosted glassmorphism, tonal containment, and ultra-subtle perimeter borders rather than heavy ambient drop shadows.

- **Layer 0 (Canvas Base):** Solid `#161618`. Houses inactive gutter spaces and root application frame.
- **Layer 1 (Inset Containers & Sidebars):** `#1C1C1E` with a backdrop blur of `20px` (where translucent vibrancy is enabled) and a 1px structural stroke of `rgba(255, 255, 255, 0.08)`.
- **Layer 2 (Active Cards & Segmented Plates):** `#242428` layered with a crisp top edge specular highlight (`rgba(255, 255, 255, 0.05) inset 0 1px 0 0`) to replicate physical machined edges.
- **Layer 3 (Popovers, Context Menus & Tooltips):** Frosted `#2C2C30` with `backdrop-filter: blur(30px) saturate(180%)`, a 1px border of `rgba(255, 255, 255, 0.12)`, and a discrete diffuse shadow: `0 12px 32px rgba(0, 0, 0, 0.45)`.
- **Dividers:** 1px hairline horizontal rules using `rgba(255, 255, 255, 0.08)`, indented by `16px` on the leading edge to match native Apple Settings row dividers.

## Shapes
The design adopts macOS-standard squircle curves and pill-shaped interactive components, avoiding jagged edges and sharp corners.

- **Desktop Inset Group Cards:** `rounded-lg` (16px / 1rem) for card envelopes, maintaining soft nested corners.
- **Inner Controls & Buttons:** `rounded` (8px / 0.5rem) for text fields, interactive buttons, and dropdown pickers.
- **Pill Elements:** Fully rounded caps (`9999px`) reserved for segmented control indicators, status badges, telemetry flags, and live audio waveform bars.
- **Window Frame:** Native macOS outer radius (10px - 12px) respecting operating system window chrome.

## Components

### 1. Inset Grouped Settings Tables
- Formed with a `#1C1C1E` background, `16px` border radius, and 1px `rgba(255, 255, 255, 0.08)` border.
- Each row has a minimum height of `44px`, horizontal padding of `16px`, and an indented 1px hairline divider (`rgba(255, 255, 255, 0.08)`) between cells that stops short of the outer edge.
- Rows feature an icon slot (28px square with rounded-sm background), title, optional subtitle, and right-aligned detail text or trailing control.

### 2. Segmented Controls
- Exterior track container styled with `#161618`, padded with `2px`, and rounded to `8px`.
- Active segment is an elevated sliding `#242428` pill with `rgba(255, 255, 255, 0.12)` border and subtle top specular sheen.
- Text switches smoothly from `#8E8E93` (inactive) to `#FFFFFF` (active).

### 3. macOS Switches & Checkboxes
- **Toggle Switches:** Width `38px`, height `22px`. Off-state is `#2C2C30`; active on-state fills with `#30D158` (for engine/safety triggers) or `#0A84FF` (for assistant features). Thumb is solid white `#FFFFFF` with a soft 1px shadow.
- **Checkboxes:** 16px square with `4px` corner radius. Border `1.5px solid rgba(255, 255, 255, 0.2)` when unchecked; fills with `#0A84FF` and a white check icon when checked.

### 4. Buttons
- **Primary:** Background `#0A84FF`, hover `#0071E3`, text `#FFFFFF`, radius `8px`, height `32px`, font `headline-sm`.
- **Secondary / Ghost:** Background `rgba(255, 255, 255, 0.06)`, border `1px solid rgba(255, 255, 255, 0.08)`, text `#FFFFFF`. Hover changes background to `rgba(255, 255, 255, 0.10)`.
- **Destructive:** Background `rgba(255, 69, 58, 0.15)`, text `#FF453A`, border `1px solid rgba(255, 69, 58, 0.3)`.

### 5. Input Fields
- Background `#161618`, border `1px solid rgba(255, 255, 255, 0.12)`, radius `8px`, height `32px`, horizontal padding `10px`.
- Focused state features a crisp blue outer glow: `box-shadow: 0 0 0 3px rgba(10, 132, 255, 0.35)` and border color `#0A84FF`.

### 6. Voice Waveform & Fitness-Style Rings
- **Siri/Engineer Waveform:** A horizontal sound bar strip composed of 24-32 vertical pill bars (width `3px`, gap `2px`, radius `9999px`) dynamically scaling along the Y-axis. Inactive bars rest at `#2C2C30`; speaking state activates an `#0A84FF` to `#30D158` gradient.
- **Tire Wear & Fuel Rings:** Circular metric gauges utilizing Apple Fitness styling with round caps, a track background of `rgba(255, 255, 255, 0.08)`, and foreground strokes in appropriate semantic colors (`#30D158`, `#FF9F0A`, `#FF453A`).

### 7. Telemetry Data Cells
- Distinct compact rectangular readouts displaying a secondary metric label (`label-sm`, uppercase `#8E8E93`) stacked directly above a live monospaced data string (`telemetry-display` or `telemetry-body`).
- Background features `rgba(255, 255, 255, 0.02)` fill with no harsh outline, keeping data readable and uncluttered.