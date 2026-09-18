You are the PRIMARY implementation agent for this project.

Your job is to REIMPLEMENT the application's UI so it matches the approved Google Stitch design as closely as practical in native Qt 6 / QML.

The Stitch design is the SOURCE OF TRUTH for the UI.

Read these first:
- AGENTS.md
- project documentation
- everything under /design
- DESIGN.md
- all Stitch screenshots/reference exports

Then inspect the CURRENT implementation before editing anything.

==================================================
ABSOLUTE RULE: DO NOT FAKE THE REDESIGN
==================================================

Do NOT "approximate" the Stitch design by decorating the existing UI.

Specifically, DO NOT:
- add outlines/borders around old components just to make them look closer
- put new rectangles/cards on top of incorrect old layouts
- wrap existing wrong components in decorative containers instead of replacing them
- add arbitrary separators, frames, shadows, gradients, or backgrounds that do not exist in Stitch
- keep an incorrect component hierarchy merely because it is already implemented
- leave the old design underneath and visually patch over it
- implement screenshots as static images
- use WebView/HTML/CSS to display the Stitch export
- create placeholder UI and call the task complete

If the existing layout fundamentally differs from Stitch:
DELETE/REFACTOR the relevant QML layout and rebuild it properly.

An edit is only successful when the ACTUAL component structure, spacing, sizing,
typography, hierarchy, navigation and visual behavior match Stitch.

==================================================
DESIGN FIDELITY
==================================================

Match Stitch as closely as possible:

- page structure
- component hierarchy
- sidebar/navigation
- spacing
- margins/padding
- typography hierarchy
- font weights
- corner radii
- control dimensions
- alignment
- icon placement
- cards/groups only where Stitch actually has them
- backgrounds/materials
- opacity
- shadows
- state colors
- hover/pressed/selected states
- charts/status areas
- empty states
- scroll behavior
- transitions/animations

Do not invent a new design language.

The target is:
"native QML implementation of the Stitch design",
NOT
"existing app with some Stitch-inspired styling".

Prefer reusable components and design tokens, for example:
- Theme.qml
- AppSidebar.qml
- SettingsGroup.qml
- StatusCard.qml
- SectionHeader.qml
- shared controls where appropriate

But do not over-abstract tiny one-off components.

==================================================
FEATURE RULES
==================================================

Stitch controls VISUAL DESIGN.

The existing application/backend controls existing FUNCTIONALITY.

Therefore:

1. If a feature exists in BOTH Stitch and the current app:
   -> implement it using the Stitch UI.

2. If Stitch shows a feature that the current app does NOT have:
   -> implement the feature if reasonably possible and connect it properly.
   -> do not create a dead fake control.

3. If the current app has an important feature that Stitch does NOT show:
   -> KEEP the feature.
   -> redesign its UI so it fits naturally into the Stitch design language.
   -> do not silently delete functionality.

4. Preserve working backend systems unless a UI integration genuinely requires changes:
   - telemetry
   - RaceState / RaceHistory
   - EventEngine
   - SpotterEngine
   - audio
   - VAD/STT/TTS
   - LLM provider/tool system
   - settings persistence
   - game integration

Do NOT rewrite working backend code merely to make UI implementation easier.

==================================================
SCREENS
==================================================

Implement the entire approved UI, not only the currently visible screen.

At minimum inspect/implement all existing Stitch designs for:

- Dashboard
- Engineer
- Telemetry
- Voice
- AI
- Settings

Navigation between every page must work.

Do not stop after implementing the sidebar or first page.

==================================================
IMPLEMENTATION METHOD
==================================================

Work in two passes.

PASS 1 — STRUCTURE

Implement:
- correct page/component hierarchy
- navigation
- reusable QML components
- data bindings
- interactions
- settings
- missing functional controls
- responsive sizing where needed

The result must already be functional.

PASS 2 — VISUAL FIDELITY

Compare every screen against its Stitch reference and fix:
- geometry
- spacing
- alignment
- typography
- component size
- radius
- opacity
- visual hierarchy
- iconography
- states
- animations
- unnecessary elements

Do NOT use borders/outlines as a shortcut for geometry or hierarchy problems.

If something looks wrong, first ask:
"Is the component/layout structure itself wrong?"

Fix structure before adding styling.

==================================================
SCREENSHOT-DRIVEN REVIEW
==================================================

For EACH page:

1. Open/read the Stitch reference.
2. Inspect the corresponding QML page.
3. Identify structural differences.
4. Fix those differences.
5. Build/run if possible.
6. Compare the implementation with the reference again.
7. Fix remaining visible discrepancies.

Do not declare success merely because the project compiles.

"Build succeeds" != "UI matches Stitch".

==================================================
QUALITY BAR
==================================================

Before finishing, explicitly verify:

- no old UI accidentally remains underneath the new UI
- no decorative outline hacks were added
- no duplicated cards/containers
- no fake controls
- no broken bindings
- no removed existing functionality
- every page is reachable
- controls use consistent design tokens
- layouts behave correctly at the app's intended window sizes
- no obvious QML warnings
- project builds where supported
- screenshots/reference were actually used, not merely DESIGN.md text

Search for old/dead QML components after migration and remove them when they are
no longer used.

Do not preserve obsolete UI simply to minimize the diff.

A larger clean refactor is preferable to a small incorrect visual patch.

==================================================
WHEN SOMETHING IS AMBIGUOUS
==================================================

Priority:

1. Stitch screenshot/reference
2. DESIGN.md
3. existing functional behavior
4. existing visual implementation

Do not choose the old visual implementation over Stitch simply because it is easier.

If Stitch and existing functionality conflict:
preserve the functionality but express it using the Stitch visual system.

==================================================
FINAL CHECK
==================================================

Before stopping, review EVERY Stitch screen one final time.

For each page report:
- implemented
- bindings working
- notable remaining visual differences, if any
- anything requiring real Windows/game/audio hardware validation

Do not claim pixel-perfect fidelity if differences remain.

Most importantly:

DO NOT solve visual mismatch by merely adding outlines, borders, wrappers or overlays.
REBUILD THE ACTUAL QML STRUCTURE when the structure is wrong.

If your planned change consists mainly of adding Rectangle borders, outlines,
frames, wrappers or overlays to the existing UI, STOP.

That is almost certainly the wrong implementation strategy.

Re-open the Stitch reference, compare the component hierarchy, and rebuild the
incorrect section instead.