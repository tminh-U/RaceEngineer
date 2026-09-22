Rebuild the entire Race Engineer GUI FROM SCRATCH using Dear ImGui.

The old GUI has been intentionally deleted.
Do NOT restore, reuse, imitate, or port the old QML/Qt Quick UI.

SOURCE OF TRUTH
1. Everything under `Design/`, especially the Google Stitch screenshots/HTML/exports.
2. Stitch screenshots define the exact visual target.
3. Existing C++ backend defines existing functionality.
4. REQUIREMENTS.md / AGENTS.md define project constraints.

The final application must reproduce Stitch as closely as practically possible, both VISUALLY and FUNCTIONALLY.

==================================================
TECH STACK
==================================================

Implement the GUI with native C++ + Dear ImGui.

Do NOT use:
- Qt Quick / QML
- HTML/CSS/WebView
- Electron
- embedded Stitch HTML
- screenshot-as-UI hacks

Use the existing Windows native application/backend.
Integrate Dear ImGui properly into the current CMake project.

Use a suitable Windows renderer/backend such as:
- Win32 + DirectX 11
or the existing compatible native rendering stack if one already exists.

Keep the implementation lightweight and appropriate for running alongside AC/ACC.

==================================================
STITCH MUST BE COPIED, NOT INTERPRETED
==================================================

This is NOT a redesign.

Do not make the UI:
- “Stitch inspired”
- “similar”
- “cleaner”
- “more native”
- “more ImGui-like”

Reproduce Stitch.

For every screen, match as closely as possible:
- exact overall geometry
- sidebar width
- header height
- page margins
- padding
- gaps
- card dimensions
- panel proportions
- typography hierarchy
- font size
- font weight
- text alignment
- colors
- opacity
- border thickness
- corner radius
- separators
- icons
- buttons
- switches
- combo boxes
- text fields
- sliders
- status pills
- telemetry displays
- message bubbles
- scroll areas
- hover state
- pressed state
- selected state
- disabled state
- empty state

Do NOT globally scale arbitrary values to “look better”.
Use the actual geometry from Stitch.

If Stitch says 230 px sidebar, implement 230 px.
If Stitch says 56 px header, implement 56 px.
Do not invent another size.

==================================================
FONTS AND ICONS
==================================================

Use the same typography intent as Stitch.

Where Stitch uses:
- Inter -> use Inter if available in project/runtime
- JetBrains Mono -> use JetBrains Mono for telemetry/numeric values

Load appropriate font weights into Dear ImGui.

Use proper icon assets/icon font where appropriate.
Do NOT replace Stitch icons with random Unicode characters such as:
`◉ ♬ ♙ ▦`.

If an exact icon asset exists in `Design/`, use/recreate it appropriately.

==================================================
DEAR IMGUI IMPLEMENTATION QUALITY
==================================================

Do not let the UI look like default Dear ImGui.

Create a proper reusable design layer for Stitch, e.g.:
- colors/tokens
- typography
- spacing
- rounded panels
- buttons
- status chips
- nav items
- input controls
- telemetry cards
- switches
- message bubbles

Use ImDrawList/custom rendering where standard ImGui widgets cannot reproduce Stitch accurately.

It is acceptable and expected to custom-draw components when required for fidelity.

Do NOT force everything through default `ImGui::Button`, default table styling, default frames, etc. if that makes the UI look unlike Stitch.

Do NOT solve layout issues using arbitrary invisible spacers or clipping hacks.

Calculate layout deliberately from the Stitch geometry.

==================================================
FUNCTIONALITY
==================================================

Every visible interactive feature in Stitch must actually work.

Do NOT implement dead controls.

If Stitch contains a feature that already exists in backend:
connect it.

If Stitch contains a feature that does not exist yet:
implement the real backend functionality when reasonably possible.

If an existing important backend feature is not represented in Stitch:
preserve the feature and integrate it naturally without changing the approved Stitch layout unnecessarily.

Preserve and wire existing systems including:
- AC / ACC telemetry
- RaceState / RaceHistory
- EventEngine
- SpotterEngine
- Push-to-Talk
- DirectInput wheel button mapping
- PhoWhisper STT
- LLM configuration
- OpenAI-compatible/local LLM
- tool calling
- Piper / Gwen TTS
- microphone/audio device handling
- telemetry status
- API state/statistics
- settings persistence
- tray/application lifecycle

Do NOT rewrite working backend systems unless integration genuinely requires it.

==================================================
SCREENS
==================================================

Implement ALL Stitch screens, not only Dashboard.

At minimum:
- Dashboard / Bảng điều khiển
- Engineer / Kỹ sư
- Telemetry
- AI / Trí tuệ nhân tạo
- Settings / Cài đặt

If the current Stitch export contains additional pages/features, implement those too.

Navigation must work.

==================================================
DASHBOARD
==================================================

Reproduce the Stitch Dashboard structure exactly, including where present:

- fixed sidebar
- fixed/top session header
- Quick Race Bar
- radio conversation area
- PTT / microphone state
- driver/engineer message bubbles
- text command entry
- quick radio commands
- cockpit quick settings
- system-link status panel
- footer/status area

Do NOT substitute generic large telemetry cards where Stitch uses compact cells.

==================================================
RESPONSIVENESS
==================================================

The primary target is the Stitch reference resolution/layout.

First make that target visually accurate.

Then make resizing degrade gracefully without changing the design unnecessarily.

Never sacrifice fidelity at the target resolution just to create a generic responsive layout.

==================================================
IMPLEMENTATION PROCESS
==================================================

Work SCREEN BY SCREEN.

For each screen:

1. Open the corresponding Stitch screenshot.
2. Inspect the Stitch HTML/export for exact dimensions/styles.
3. Inspect backend functionality needed by that screen.
4. Implement the Dear ImGui screen.
5. Build the application.
6. Run it.
7. Capture a screenshot of the implemented screen.
8. Compare that screenshot directly against Stitch.
9. Fix visible discrepancies.
10. Repeat until the major differences are gone.

DO NOT implement all screens blindly and only inspect them at the end.

==================================================
VISUAL VERIFICATION IS REQUIRED
==================================================

Compilation alone is NOT completion.

`build succeeded` != `matches Stitch`.

You must actually run the application and visually compare the result against the Stitch references.

Check:
- dimensions
- alignment
- overflow
- spacing
- font scale
- colors
- card heights
- clipping
- text truncation
- navigation state

If you cannot run or screenshot the application in your environment,
state that explicitly instead of claiming visual fidelity.

==================================================
STRICTLY FORBIDDEN
==================================================

Do NOT:
- reintroduce QML
- port the deleted GUI
- approximate Stitch from memory
- redesign Stitch
- use generic Dear ImGui styling
- add random borders/wrappers
- use clip regions to hide broken sizing
- use fake/static controls
- use placeholder functionality and claim completion
- replace real icons with random Unicode glyphs
- alter `Design/`
- stop after Dashboard
- claim pixel-perfect fidelity without visual comparison

`Design/` is READ ONLY.

==================================================
PRIORITY
==================================================

When there is ambiguity:

1. Stitch screenshot
2. Stitch HTML/export
3. documented requirements
4. existing backend behavior
5. implementation convenience

Implementation convenience NEVER wins over Stitch visual fidelity.

==================================================
FINAL VALIDATION
==================================================

Before finishing:

- build successfully
- run successfully
- inspect every screen
- compare every screen to Stitch
- verify navigation
- verify controls are functional
- verify backend bindings
- verify no GUI overflow
- verify no dead controls
- verify no old QML GUI was restored
- verify `Design/` was untouched

Report only:
- files added/changed
- backend integrations added
- screens implemented
- build/test result
- which screens were visually verified
- any remaining mismatch/blocker

Do the implementation now.
Do not only write a plan.