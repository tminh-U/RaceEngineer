---
name: Race Engineer Desktop
colors:
  surface: '#131315'
  surface-dim: '#131315'
  surface-bright: '#39393b'
  surface-container-lowest: '#0e0e10'
  surface-container-low: '#1b1b1d'
  surface-container: '#1B1B1D'
  surface-container-high: '#2a2a2c'
  surface-container-highest: '#353437'
  on-surface: '#e5e1e4'
  on-surface-variant: '#c0c6d6'
  inverse-surface: '#e5e1e4'
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
  on-background: '#e5e1e4'
  surface-variant: '#353437'
  surface-canvas: '#131315'
  surface-elevated: '#242428'
  surface-overlay: '#2C2C30'
  stroke-subtle: rgba(255, 255, 255, 0.08)
  stroke-strong: rgba(255, 255, 255, 0.14)
  text-primary: '#FFFFFF'
  text-secondary: '#8E8E93'
  text-tertiary: rgba(142, 142, 147, 0.60)
  status-critical: '#FF453A'
typography:
  display-lg:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Display', 'SF Pro Text',
      'SF Pro', 'Helvetica Neue', sans-serif
    fontSize: 28px
    fontWeight: '600'
    lineHeight: 34px
    letterSpacing: -0.02em
  headline-lg:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Display', 'SF Pro Text',
      'SF Pro', 'Helvetica Neue', sans-serif
    fontSize: 20px
    fontWeight: '600'
    lineHeight: 26px
    letterSpacing: -0.015em
  headline-sm:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'SF Pro', 'Helvetica
      Neue', sans-serif
    fontSize: 15px
    fontWeight: '600'
    lineHeight: 20px
    letterSpacing: -0.01em
  body-lg:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'SF Pro', 'Helvetica
      Neue', sans-serif
    fontSize: 14px
    fontWeight: '400'
    lineHeight: 20px
    letterSpacing: -0.005em
  body-sm:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'SF Pro', 'Helvetica
      Neue', sans-serif
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
    letterSpacing: 0em
  telemetry-display:
    fontFamily: JetBrains Mono, SF Mono, Menlo, monospace
    fontSize: 22px
    fontWeight: '500'
    lineHeight: 26px
    letterSpacing: -0.02em
  telemetry-body:
    fontFamily: JetBrains Mono, SF Mono, Menlo, monospace
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
    letterSpacing: -0.01em
  label-md:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'SF Pro', 'Helvetica
      Neue', sans-serif
    fontSize: 12px
    fontWeight: '500'
    lineHeight: 16px
    letterSpacing: 0.02em
  label-sm:
    fontFamily: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'SF Pro', 'Helvetica
      Neue', sans-serif
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
The design system establishes a high-precision, focused motorsport telemetry and race engineering environment calibrated for instantaneous decision-making during high-stress competition. Drawing directly from native Apple desktop human interface paradigms, macOS system architecture, and professional digital audio workstation layouts, the interface replaces aggressive racing graphics with structural composure, deep neutral surfaces, and legible information hierarchy.

Key attributes:
- **Native macOS Fidelity:** Rooted in San Francisco typographic rhythm, inset grouped panels, vibrancy-ready surface tiers, and refined controls that feel at home on desktop operating systems.
- **Deep Neutral Discipline:** Deep charcoal surfaces isolate critical telemetry data without inducing visual fatigue during prolonged sessions in low-light simulator rigs.
- **Calm Instrumental Precision:** Telemetry, strategy timelines, and voice communication indicators present high-density information through strict monospaced data alignment and instant color recognition.

## Colors
The color palette relies on low-reflectance, high-contrast dark mode fundamentals paired with standard Apple system accents. Surfaces step gracefully from deep charcoal canvas foundations up through interactive cards and floating popovers.

- **Primary Canvas & Insets:** The canvas background starts at `#131315`, with primary grouped containers and tables placed at `#1B1B1D` and active panels at `#242428`.
- **System Accent (`#0A84FF`):** Drives focus states, active segmented controls, primary calls to action, and live telemetry channel selections.
- **Status & Telemetry Accents:**
  - **Optimal / Green (`#30D158`):** Green flag, optimal tire core temperature, positive sector delta, connected telemetry link.
  - **Caution / Amber (`#FF9F0A`):** Brake fade warnings, fuel conservation mode, yellow flag advisory.
  - **Critical / Red (`#FF453A`):** Mechanical failure warning, delta negative, pit speed violation.
- **Hairlines & Dividers:** Built using semi-transparent white overlays (`rgba(255, 255, 255, 0.08)`) to maintain contrast without harsh border lines across varied surface tiers.

## Typography
Typographic hierarchy uses Apple's native San Francisco system font stack (`-apple-system`, `BlinkMacSystemFont`, `SF Pro Display`, `SF Pro Text`) across all narrative, navigational, and administrative interfaces. This is paired with `JetBrains Mono` (or system monospace fallbacks) for tabular metrics, time-deltas, gear indicators, and high-frequency telemetry outputs.

- **Proportional UI Text:** Titles and primary controls leverage San Francisco with negative tracking at larger sizes to create the crisp, compact silhouette typical of macOS Monterey, Ventura, and Sonoma utility windows.
- **Tabular Data Rules:** Any numerical values that fluctuate in real time (e.g., speed, fuel liters remaining, lap delta, suspension travel) must use `JetBrains Mono` or apply `font-variant-numeric: tabular-nums` to eliminate jitter and horizontal layout shift.
- **Micro-Labels & Metadata:** Group header titles and telemetry sensor tags use `label-sm` in uppercase with subtle positive letter spacing (`0.04em`) rendered in muted text secondary (`#8E8E93`).

## Layout & Spacing
The layout follows a multi-pane desktop viewport system structured around a macOS unified toolbar, navigation sidebar, main track canvas, and an engineering inspector panel.

- **Window Layout Rhythm:**
  - Standard desktop displays allocate 240px to the left-side navigation, a fluid central work area for circuit telemetry and graphs, and 320px to the right-hand engineer strip.
  - `margin-window` (12px / 0.75rem) guarantees a clean offset between macOS native window borders and inner split containers.
  - Inset grouped containers utilize `space-md` (12px) horizontal and vertical padding, keeping the density high without feeling cramped.
- **Responsive & Scaling Behaviors:**
  - **Wide Displays (>1440px):** All three panes remain visible simultaneously with multi-track telemetry comparison visible in the center stage.
  - **Standard Desktop (1024px – 1439px):** The engineer inspector panel becomes collapsible via a window toolbar toggle or slides out as an elevated overlay panel.
  - **Compact Displays (<1024px):** The left sidebar collapses into an icon rail (48px width) and grouped cards stack vertically inside the main workspace.

## Elevation & Depth
Elevation mimics the layered visual architecture of macOS dark mode, using stacked tonal surfaces, specular border highlights, and controlled backdrop blurs rather than heavy drop shadows.

- **Layer 0 (Canvas Base):** Deep `#131315` matte background containing gutters and window framing.
- **Layer 1 (Inset Containers & Sidebars):** `#1B1B1D` with optional `backdrop-filter: blur(20px)` and a subtle outer stroke (`1px solid rgba(255, 255, 255, 0.08)`).
- **Layer 2 (Elevated Cards & Segmented Plates):** `#242428` featuring an inset hairline highlight on the top edge (`inset 0 1px 0 0 rgba(255, 255, 255, 0.06)`) to simulate machined physical depth.
- **Layer 3 (Modals, Popovers & Context Menus):** Translucent `#2C2C30` with `backdrop-filter: blur(30px) saturate(180%)`, a refined border of `rgba(255, 255, 255, 0.14)`, and a diffused shadow: `0 16px 36px rgba(0, 0, 0, 0.5)`.
- **Separators:** Inset 1px hairlines using `rgba(255, 255, 255, 0.08)` indented 16px from the leading edge inside grouped lists.

## Shapes
The design adopts macOS-standard squircle curves and pill-shaped interactive components, avoiding jagged edges and sharp corners.

- **Inset Group Envelopes:** `rounded-lg` (16px / 1rem) for card envelopes, maintaining soft nested corners that harmonize with Apple desktop windows.
- **Interactive Controls:** `rounded` (8px / 0.5rem) for standard buttons, input fields, popover frames, and segmented tracks.
- **Pill Elements:** Fully rounded radius (`9999px`) reserved for status badges, segmented control highlight pills, telemetry delta tags, and audio waveform bars.
- **Window Frame:** 10px–12px outer radius adhering strictly to macOS window chrome standards.

## Components

### 1. Inset Grouped Settings & Telemetry Tables
- Container uses `#1B1B1D` background with `16px` border radius and `1px solid rgba(255, 255, 255, 0.08)` border.
- Rows have a minimum height of `44px`, horizontal padding of `16px`, and an indented `1px` divider (`rgba(255, 255, 255, 0.08)`) between rows.
- Each row includes an optional leading icon, San Francisco primary title, optional secondary text, and a right-aligned control (toggle, chevron, or telemetry badge).

### 2. Segmented Controls
- Outer container styled with `#131315`, `2px` internal padding, and `8px` corner radius.
- Active segment is an elevated `#242428` pill with a `1px` border of `rgba(255, 255, 255, 0.12)` and light specular top sheen.
- Labels transition smoothly from `#8E8E93` (inactive) to `#FFFFFF` (active) with `font-weight: 500`.

### 3. Native macOS Toggles & Checkboxes
- **Toggle Switches:** Width `38px`, height `22px`. Inactive track is `#2C2C30`; active track fills with `#30D158` (for powertrain/active systems) or `#0A84FF` (for assistant parameters). Circular thumb is `#FFFFFF` with a 1px soft ambient shadow.
- **Checkboxes:** 16px square with `4px` corner radius. Unchecked state has `1.5px solid rgba(255, 255, 255, 0.2)` border; checked state fills with `#0A84FF` featuring a crisp white SVG check icon.

### 4. Push Buttons
- **Primary Button:** Solid `#0A84FF` fill with white text, `8px` corner radius, `32px` height, and `headline-sm` font weight (`600`). On hover, background shifts to `#0071E3`.
- **Secondary / Ghost Button:** `rgba(255, 255, 255, 0.06)` background, `1px solid rgba(255, 255, 255, 0.08)` stroke, `#FFFFFF` text.
- **Destructive Button:** `rgba(255, 69, 58, 0.15)` tinted fill, `#FF453A` text, and `1px solid rgba(255, 69, 58, 0.3)` stroke.

### 5. Input Fields & Search Bars
- Background `#131315`, border `1px solid rgba(255, 255, 255, 0.12)`, radius `8px`, height `32px`, horizontal padding `10px`.
- Focus state applies a macOS-standard accent ring: `box-shadow: 0 0 0 3px rgba(10, 132, 255, 0.35)` with a `#0A84FF` border.

### 6. Dynamic Voice Waveforms & Activity Rings
- **Engineer Speech Waveform:** Horizontal sound strip built from 24–32 vertical pill bars (width `3px`, gap `2px`, radius `9999px`). Resting state displays `#2C2C30`; live transmission drives active animated height scaling in `#0A84FF` shifting to `#30D158`.
- **Tire Wear & Fuel Gauges:** Apple Fitness-style concentric circular meters with smooth rounded caps, faint background tracks (`rgba(255, 255, 255, 0.08)`), and colored foreground strokes keyed to telemetry conditions.

### 7. Monospaced Telemetry Data Cells
- Inset cards displaying an uppercase SF Pro label (`label-sm`, `#8E8E93`) stacked directly above a live readout in `JetBrains Mono` (`telemetry-display` or `telemetry-body`).
- Background features an ultra-subtle `rgba(255, 255, 255, 0.02)` fill with no hard outline, allowing high-frequency numbers to stay legible without visual clutter.