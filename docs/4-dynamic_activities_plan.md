# Dynamic Activities Development Plan

**Version:** 1.0
**Date:** 2026-04-21
**Spec reference:** `docs/1-specification.md`, `docs/2-development_plan.md` (Feature 8)

---

## Table of Contents

- [Overview](#overview)
- [Dynamic Activities Catalogue](#dynamic-activities-catalogue)
- [Flash Storage Constraints](#flash-storage-constraints)
- [Activity 1 — Space Meteor Shower](#activity-1--space-meteor-shower)
  - [Concept](#concept)
  - [Math Problem Rules](#math-problem-rules)
  - [Timing & Scoring Model](#timing--scoring-model)
  - [Credit Calculation](#credit-calculation)
  - [URL Parameters & API Integration](#url-parameters--api-integration)
  - [Security Considerations](#security-considerations)
  - [Phase A1.1 — Game Design & UI Specification](#phase-a11--game-design--ui-specification)
  - [Phase A1.2 — HTML/JS/CSS Implementation](#phase-a12--htmljscss-implementation)
  - [Phase A1.3 — Integration & Verification](#phase-a13--integration--verification)
- [Activity 2 — Clown Fish Bubble Burst](#activity-2--clown-fish-bubble-burst)
  - [Concept](#concept-1)
  - [Math Problem Rules](#math-problem-rules-1)
  - [Timing & Scoring Model](#timing--scoring-model-1)
  - [Credit Calculation](#credit-calculation-1)
  - [URL Parameters & API Integration](#url-parameters--api-integration-1)
  - [Security Considerations](#security-considerations-1)
  - [Phase A2.1 — Game Design & UI Specification](#phase-a21--game-design--ui-specification)
  - [Phase A2.2 — HTML/JS/CSS Implementation](#phase-a22--htmljscss-implementation)
  - [Phase A2.3 — Integration & Verification](#phase-a23--integration--verification)
- [Activity 3 — Rocket Asteroid Shooter](#activity-3--rocket-asteroid-shooter)
  - [Concept](#concept-2)
  - [Math Problem Rules](#math-problem-rules-2)
  - [Timing & Scoring Model](#timing--scoring-model-2)
  - [Credit Calculation](#credit-calculation-2)
  - [URL Parameters & API Integration](#url-parameters--api-integration-2)
  - [Security Considerations](#security-considerations-2)
  - [Phase A3.1 — Game Design & UI Specification](#phase-a31--game-design--ui-specification)
  - [Phase A3.2 — HTML/JS/CSS Implementation](#phase-a32--htmljscss-implementation)
  - [Phase A3.3 — Integration & Verification](#phase-a33--integration--verification)
- [Activity 4 — Math Battleship: Grid Commander](#activity-4--math-battleship-grid-commander)
  - [Concept](#concept-3)
  - [Math Problem Rules](#math-problem-rules-3)
  - [Timing & Scoring Model](#timing--scoring-model-3)
  - [Credit Calculation](#credit-calculation-3)
  - [URL Parameters & API Integration](#url-parameters--api-integration-3)
  - [Security Considerations](#security-considerations-3)
  - [Phase A4.1 — Game Design & UI Specification](#phase-a41--game-design--ui-specification)
  - [Phase A4.2 — HTML/JS/CSS Implementation](#phase-a42--htmljscss-implementation)
  - [Phase A4.3 — Integration & Verification](#phase-a43--integration--verification)
- [Dependency Notes](#dependency-notes)
- [Summary of Design Decisions](#summary-of-design-decisions)

---

## Overview

This document specifies `main/dyn_activities/space_math.html` — a self-contained
HTML/JS/CSS dynamic activity (mini-game) for the esport-fi32 platform targeting children
aged approximately 8 years old.

The game exercises fast mental arithmetic.  Kids solve as many math problems as possible
within a time limit to earn internet time credits.  Problems are intentionally simple and
the answer input method minimises typing overhead so that credit is awarded based on mental
speed, not keyboard proficiency.

The game file is a single self-contained `.html` file placed in `main/dyn_activities/`.
It is embedded in the firmware binary at build time by the existing CMake infrastructure
(Phase 8.3 of Feature 8) and served by `GET /dyn_activities/space_math`.  No server
code changes are required.

---

## Dynamic Activities Catalogue

| # | Name | File | Status | Phases |
| - | ---- | ---- | ------ | ------ |
| 1 | Space Meteor Shower | `space_math.html` | Implemented | A1.1, A1.2, A1.3 |
| 2 | Clown Fish Bubble Burst | `fish_math.html` | Implemented | A2.1, A2.2, A2.3 |
| 3 | Rocket Asteroid Shooter | `shoot_math.html` | Implemented | A3.1, A3.2, A3.3 |
| 4 | Math Battleship: Grid Commander | `battle_math.html` | Implemented | A4.1, A4.2, A4.3 |

> To add a new activity: assign it the next available number, add a row to this table,
> and create its own `## Activity N` section followed by phases `AN.1` (Design),
> `AN.2` (Implementation), and `AN.3` (Integration).

---

## Flash Storage Constraints

All dynamic activity HTML files are embedded directly in the firmware binary at build time
via the ESP-IDF `EMBED_FILES` mechanism.  Flash on the ESP32-C6 is a shared, finite
resource — the firmware image, NVS partitions, OTA slots, and all embedded game files
must fit within the partition table allocation.

### Per-File Size Budget

| Activity | File | Uncompressed target | Uncompressed maximum | Compressed estimate |
| -------- | ---- | ------------------- | -------------------- | ------------------- |
| 1        | `space_math.html` | < 40 KB | < 80 KB | ~ 10–15 KB |
| 2        | `fish_math.html`  | < 40 KB | < 80 KB | ~ 10–15 KB |
| 3        | `shoot_math.html` | < 40 KB | < 80 KB | ~ 10–15 KB |
| 4        | `battle_math.html` | < 200 KB | < 400 KB | < 100 KB |

> **Activity 4 — relaxed size budget.**  `battle_math.html` has a richer visual design
> (SVG ship silhouettes, sonar radar animation, per-cell state graphics) that justifies a
> larger source file.  The **hard limit is a compressed size of < 100 KB** in the firmware
> binary artefact — this is the figure that matters for flash allocation.  The uncompressed
> cap of 400 KB keeps the source file human-readable in a text editor.  All other rules
> from §Rules for Controlling File Size (no minification, no external resources, no
> base64 binary assets, vanilla JS/CSS only) still apply.

> **Note**: the size targets and hard maximum apply to the **uncompressed source file**
> in the repository.  The figure that directly determines flash consumption is the
> compressed size embedded in the firmware.  HTML/JS/CSS compresses very well with gzip
> (typically 65–80 % reduction); the uncompressed cap is retained to keep source files
> readable and maintainable, not as a flash budget limit.
>
> Feature 9 (`docs/2-development_plan.md`) describes the build infrastructure that
> gzip-compresses all files in `main/dyn_activities/` at CMake configure time before
> embedding them in the firmware binary.

### Rules for Controlling File Size (Without Obfuscation)

These rules apply to every dynamic activity `.html` file.  The goal is a small, readable,
maintainable source file — **not** a minified blob.

1. **No minification.**  The source file in the repository must be human-readable.
   Minification is explicitly prohibited to preserve long-term maintainability.
2. **No external resources.**  No CDN links, remote fonts, or remote images.  All CSS,
   JS, and graphics must be inline.
3. **No polyfills.**  Target ES2015 (ES6) — all modern tablet and smartphone browsers
   support it natively.  No transpiled or down-level code.
4. **No base64-encoded binary assets.**  Use inline SVG or pure CSS shapes and gradients.
5. **No CSS or JS frameworks** (Bootstrap, Tailwind, jQuery, lodash, etc.).
   Vanilla HTML, CSS, and JS only.
6. **CSS custom properties** (`--var: value`) for repeated colour values — avoids
   duplicating hex codes across rules.
7. **Minimal comments.**  A brief inline comment per logical section is acceptable;
   large documentation blocks are not.
8. **Shared CSS classes with modifiers** — avoid writing a unique rule for every element.
9. **Prefer CSS animations over JS-driven animation** for visual effects — GPU-accelerated
   and typically expressed in fewer source lines.
10. **`const` and `let` only** — no `var`; no unused variable declarations.

---

## Activity 1 — Space Meteor Shower

### Concept

The player is a space pilot defending their planet from falling meteors.
Each meteor carries a math problem.
The player must type the correct answer and press **Enter** (or tap **OK** on the
on-screen keypad) before the meteor reaches the ground.

- Solving a meteor correctly **destroys** it with a brief animated explosion (CSS keyframe).
- A wrong answer flashes the meteor red and deducts 1 second from the countdown timer
  as a penalty — this keeps the game fair without humiliating the child.
- If the meteor touches the ground without a correct answer it explodes in a "damage" colour
  and the game continues; the player does not lose a life — avoiding frustration for younger
  kids.  The problem is simply lost (counts as unsolved).

Engagement hooks:
- Moving SVG/CSS animated meteors fall at varying speeds (progressively faster as problems
  are solved correctly).
- A star-field scrolling background (pure CSS animation, no images).
- A visible countdown bar plus a large numeric timer.
- Celebratory "YOU WIN!" screen with star-burst animation when the target is reached or
  exceeded before time runs out.
- "GAME OVER" screen with proportional credit display when time expires.
- Sound is NOT used (avoids permission issues and distraction in shared spaces).

Size budget: target < 40 KB, hard maximum 80 KB (uncompressed HTML file).
See [Flash Storage Constraints](#flash-storage-constraints).  All assets are CSS/SVG — no
binary images or external resources.

---

### Math Problem Rules

| Type           | Operand ranges                             | Result constraint    | Example         |
| -------------- | ------------------------------------------ | -------------------- | --------------- |
| Addition       | Both operands 1–49                         | Sum ≤ 99             | `23 + 41 = ?`   |
| Subtraction    | Minuend 10–99, subtrahend 1–(minuend-1)    | Result ≥ 1           | `54 − 17 = ?`   |
| Multiplication | Both factors 1–9 (single-digit × single-digit) | Product ≤ 81     | `7 × 6 = ?`     |

- No division.
- All answers are positive integers.
- Problems are generated pseudo-randomly on the client side using `Math.random()`.  The
  type is chosen uniformly at random from the three categories.
- A given problem is never repeated within the same game session (a small de-dup buffer
  of the last 10 problems is maintained to avoid consecutive duplicates).

---

### Timing & Scoring Model

**Constants (hardcoded in the HTML):**

| Constant                | Value | Rationale                                                   |
| ----------------------- | ----- | ----------------------------------------------------------- |
| `SECS_PER_PROBLEM`      | 5     | Average time for an 8-year-old to answer a simple arithmetic problem with on-screen keypad input. |
| `EARN_FRACTION`         | 0.70  | 70% of `time_limit_s` is allocated to the "normal credit" problem set; remaining 30% is bonus time. |

**Target problem count** (computed from `time_limit_s` fetched from the server):

```
targetProblems = Math.floor(time_limit_s * EARN_FRACTION / SECS_PER_PROBLEM)
```

Example: `time_limit_s = 120` → `targetProblems = floor(120 × 0.70 / 5) = floor(16.8) = 16`.

**Minimum target**: `targetProblems` is clamped to at least `3` to avoid degenerate games
when `time_limit_s` is very short.

**Bonus problems** are presented indefinitely (as long as time remains) after `targetProblems`
have been solved correctly.  Each bonus problem solved earns additional proportional credit
beyond `credit_s` — however the server caps PIN-authenticated calls at `activity.credit_s`,
so the game submits `min(earnedCredits, credit_s)`.  The win screen always shows the bonus
achievement to the kid regardless.

---

### Credit Calculation

At the moment the kid clicks **Claim Credits** (or the game auto-submits on win/game-over):

```
solved       = number of problems answered correctly
earnedCredit = Math.floor(credit_s * solved / targetProblems)
submitCredit = Math.min(earnedCredit, credit_s)   // server will cap anyway
```

- `solved == 0` → `earnedCredit = 0` → `submitCredit = 0`.  The claim POST is still sent
  (server returns success with `new_counter_s` unchanged).
- `solved >= targetProblems` before time runs out → WIN state;
  `earnedCredit >= credit_s`; the bonus display shows extra achievement.
- Time runs out before `solved >= targetProblems` → GAME OVER state; proportional credit
  based on `solved`; the game is marked as a partial result.

---

### URL Parameters & API Integration

#### Launch URL (built by `/dyn` page, Phase 8.4)

The `/dyn` page currently builds launch URLs with these query parameters
(from `GET /api/dyn` response):

```
/dyn_activities/space_math?pin=<PIN>&device_idx=<N>&act_id=<ID>&credits_s=<CREDIT_S>&token=<TOKEN>
```

`time_limit_s` is **not** in the current URL template.  Two options exist:

| Option | Description | Recommendation |
| ------ | ----------- | -------------- |
| A — URL param | `/dyn` page appends `&time_limit_s=<N>` to launch URLs | Simple; requires a one-line change in `http_server_dyn.c` Phase 8.4 task 2b |
| B — API fetch | Game fetches `GET /api/dyn?device_idx=<N>` at startup and finds its own entry by `act_id` | No server change; slightly more network round-trips; slightly harder to tamper |

**Recommendation: Option A.**

Security note: `time_limit_s` is informational and used only to derive `targetProblems`
client-side.  A malicious user could modify it in the URL to change the number of problems,
but the credit amount is computed from `targetProblems` and then capped server-side at
`activity.credit_s` — there is no incentive to manipulate it.  A tampered-down
`time_limit_s` would give fewer problems for the same credit, which only disadvantages the
honest player.  This is acceptable for a home-network family game device.

#### Game startup sequence

1. Parse `pin`, `device_idx`, `act_id`, `credits_s`, `time_limit_s`, `token` from
   `URLSearchParams`.
2. Validate: all required params present and non-zero; if missing, show an error screen
   ("Oops! Missing game parameters. Please go back to the games page.") with a
   `← Back to Games` link pointing to `/dyn`.
3. Derive `targetProblems` from `time_limit_s`.
4. Start the game immediately — no additional API call required before play.

#### Credit claim POST

When the game ends (WIN or GAME OVER) a **"Claim Credits"** button is shown.  On click:

```javascript
POST /api/activities/credit
{
  "device_idx": deviceIdx,
  "act_id":     actId,
  "credits_s":  submitCredit,     // proportional, capped at credit_s
  "completion_time_s": elapsedS,  // actual seconds spent
  "pin":        pin,
  "token":      token
}
```

Response handling:
| HTTP status | Game displays |
| ----------- | ------------- |
| 200 OK      | Green banner: "Credits added! Counter: `new_counter_hms`" |
| 403         | Red banner: "Not authorised — please go back and start a new game." |
| 429         | Orange banner: "Daily limit reached — come back tomorrow!" |
| other       | Red banner with a **Try again** button (retriable). |

Credits are submitted **automatically** as soon as the END screen is shown — no user
action is required.  A "Saving your credits…" message is displayed while the request
is in flight.  For non-retriable errors (403, 429) the error banner replaces the message
and the player can navigate away.  For retriable errors a **Try again** button is shown.

---

### Security Considerations

- `pin` and `token` are passed in the URL.  On a home local network (reward AP) this is
  acceptable — TLS is not in scope for this device.  The token is one-time-use and the PIN
  is device-bound; replay attacks within the same session are prevented by the nonce system.
- `time_limit_s` in the URL is informational only; the server enforces the credit cap.
- `credits_s` in the URL is the reference maximum (`activity.credit_s`).  The game computes
  `submitCredit ≤ credits_s`; the server re-caps at `activity.credit_s` for PIN calls.
  There is no gain for a kid to forge a higher `credits_s` in the URL.
- The game generates problems purely client-side.  There is no server-side problem
  validation — the server trusts the `completion_time_s` and `credits_s` values as
  informational only, enforcing only the credit cap.

---

## Phase A1.1 — Game Design & UI Specification (Activity 1)

### Goal

Define the full visual layout, animation behaviour, screen states, and input method for
`space_math.html` so that the implementation phase (A1.2) can proceed without ambiguity.

### Screen States

The game has five mutually exclusive screen states managed by a JS state machine:

| State       | Trigger                               | Description                                           |
| ----------- | ------------------------------------- | ----------------------------------------------------- |
| `LOADING`   | Page load                             | Validates URL params; transitions to `SETUP` or `ERROR` |
| `ERROR`     | Missing/invalid URL params            | Shows error message + back link                       |
| `SETUP`     | After `LOADING` succeeds              | Problem-type selection screen; player chooses which operation types to practise, then presses **Start Game!** |
| `PLAYING`   | Player presses **Start Game!**        | Main game loop                                        |
| `END`       | Timer reaches 0 OR `solved >= targetProblems` | Shows WIN or GAME OVER result + Claim Credits button |

All states are rendered in the **same single `<div id="screen">`** by swapping its
`innerHTML` — no multi-page navigation.

### SETUP State Layout

Displayed immediately after URL-param validation succeeds, before gameplay begins.

```
┌──────────────────────────────┐
│   ★  Space Math              │
│                              │
│  Choose which types of       │
│  problems to include:        │
│                              │
│  ☑  Table multiplication     │
│  ☑  Addition                 │
│  ☑  Subtraction              │
│                              │
│  [ Start Game! ]  ← disabled │
│                  if none ✓   │
└──────────────────────────────┘
```

- All three checkboxes are **checked by default**.
- The **Start Game!** button is **disabled** when no checkbox is checked; re-enabled as
  soon as at least one is checked.
- Tapping the button records the selected types into `selectedTypes[]` and transitions
  directly to `PLAYING`.
- The setup screen uses the same dark-space background (`#0a0a2e`) and gold title colour
  as the rest of the game.  Checkboxes have `accent-color` set to the game's accent blue.
  The **Start Game!** button follows the same styling as the **OK** button.

### PLAYING State Layout

The game uses a **responsive two-layout design** that switches automatically on device
orientation change, maximising the meteor play field in both orientations.

**Portrait layout** (default — phone or tablet upright):

```
┌─────────────────────────────────────┐
│ ★ Space Math  ⏱ 01:52  [====    ]   │  ← header bar
├─────────────────────────────────────┤
│                                     │
│  [meteor 27+34=?]  [meteor 8×6=?]   │  ← meteor zone
│                                     │
│       [meteor 71-29=?]              │
│                                     │
├─────────────────────────────────────┤
│  Solved: 5 / 16       Wrong: 2      │  ← stats bar
├─────────────────────────────────────┤
│       [  _ _ _  ]  [✓ OK]           │  ← answer input + submit
│   [ 1 ][ 2 ][ 3 ]                   │
│   [ 4 ][ 5 ][ 6 ]                   │  ← on-screen keypad
│   [ 7 ][ 8 ][ 9 ]                   │
│        [ 0 ][ ← ]                   │
└─────────────────────────────────────┘
```

**Landscape layout** (`@media (orientation: landscape)` — two-column split):

The keypad moves to a fixed-width right column, giving the meteor play field the full
remaining width.  This is the **preferred orientation for tablets**.

```
┌─────────────────────────────────┬─────────────────┐
│ ★ Space Math  ⏱ 01:52  [====] │ [  _ _ _  ][ ✓ ]  │  ← header / answer input
├─────────────────────────────────┤  [ 1 ][ 2 ][ 3 ]│
│                                 │  [ 4 ][ 5 ][ 6 ]│
│  [meteor 27+34=?]               │  [ 7 ][ 8 ][ 9 ]│  ← keypad (right column)
│        [meteor 8×6=?]           │       [ 0 ][ ← ]│
│  [meteor 71-29=?]               │                 │
│                                 │  Solved: 5 / 16 │  ← stats (right column)
│                                 │  Wrong: 2       │
└─────────────────────────────────┴─────────────────┘
  ←── game column  (flex: 1 1 0) ──→  ←── 200 px ──→
```

**Active problem selection**: only one meteor is "active" (highlighted with a pulsing glow)
at a time.  By default the active meteor is the lowest/fastest-falling one.  The player
can also **tap or click any meteor** to make it the active target — useful when an
earlier problem is easier or more urgent.  Switching active meteor clears the current
answer input.

**Answer input**: a single `<input type="text" inputmode="numeric" pattern="[0-9]*">`
with `maxlength="2"` (all answers are 1–81, at most two digits).  The on-screen keypad
appends digits.  Backspace clears the last digit.  Enter / OK submits the answer.
The physical keyboard also works (number keys, Enter, Backspace).

### Meteor Visual Specification

Each meteor is a `<div>` absolutely positioned inside the game area:
- Shape: CSS oval/ellipse (`border-radius: 50%`) with a CSS radial gradient giving a
  3D rock appearance (dark centre, lighter edge).
- Size: `80px × 50px` (desktop); scales down slightly on narrow screens.
- Text: the problem string centred in bold white, font-size `1.1em`.
- Movement: CSS `animation: fall linear` with duration calculated per meteor (see below).
- Active meteor: `box-shadow: 0 0 18px 6px #ffe94a` pulsing glow.
- Correct answer: instantly replaced by a CSS `@keyframes` explosion div (orange → transparent
  radial burst, 400 ms) then removed from DOM.
- Wrong answer: `background` flashes red for 300 ms, then returns to normal.
- Reaches bottom without answer: same explosion animation as correct but in a red/purple
  colour; meteorite damage — no life lost.

### Meteor Spawn & Speed Rules

- Maximum 3 meteors on screen simultaneously.
- A new meteor is spawned:
  - At game start: first meteor immediately, second after 4 000 ms, third after 8 000 ms.
    This spaces the initial three meteors ~1/3 of the screen height apart so the child can
    focus on one problem at a time before the next appears.
  - After a meteor is destroyed or reaches the bottom: next meteor spawns immediately;
    if multiple slots are empty they are refilled with 800 ms between each spawn.
- Fall duration (time from top to bottom of game area):
  - Problems 1–5: 12 s per meteor.
  - Problems 6–10: 11 s per meteor.
  - Problems 11–20: 10 s per meteor.
  - Problems 21+: 9 s per meteor (minimum).
- Horizontal start position: random `left` between 5 % and 72 % of the game area width,
  with a minimum 24 % separation from any other meteor already on screen to prevent
  visual overlap (meteors are ~80 px wide; 24 % covers the width on typical phone screens).

### Background

Pure CSS animated star field:
- 3 layers of `<div>`s with `background-image: radial-gradient(...)` repeating dots,
  each layer animating at a different speed (CSS `@keyframes scrollStars`).
- Dark navy background (`#0a0a2e`).
- No external images.

### WIN State Layout

```
┌──────────────────────────────┐
│   🌟  YOU WIN!  🌟            │
│                              │
│  You solved 18 / 16          │
│  (+2 BONUS problems!)        │
│                              │
│  Credits earned: 0:10:00     │
│  (+ 2 bonus = 🏆 Champion!)  │
│                              │
│  [ Claim Credits ]           │
│  ← Back to Games             │
└──────────────────────────────┘
```

### GAME OVER State Layout

```
┌──────────────────────────────┐
│   💫  TIME'S UP!             │
│                              │
│  You solved 11 / 16          │
│  Credits earned: 0:06:53     │
│                              │
│  [ Claim Credits ]           │
│  ← Back to Games             │
└──────────────────────────────┘
```

### Responsiveness and Orientation

**Primary target**: tablet (768–1024 px) in either orientation; smartphone (360–414 px)
in portrait.  All layouts must function with no horizontal scrolling.

**Viewport meta tag** (required — must be exactly this):

```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
```

`user-scalable=no` prevents accidental pinch-zoom during gameplay.  `maximum-scale=1.0`
further prevents iOS from zooming when a form input is focused.

**Portrait layout** (default — stacked, `flex-direction: column`):
- Header spans full width at top.
- Meteor area (`flex-grow: 1`) fills available vertical space above the keypad panel.
- Stats bar immediately above the keypad.
- Keypad + answer input pinned at the bottom.
- Minimum supported width: 320 px.
- Meteor area height: `calc(100dvh - 220px)` (use `100vh` as fallback for older browsers).

**Landscape layout** (`@media (orientation: landscape)` — two-column, `flex-direction: row`):
- Left column (`.game-col`, `flex: 1 1 0`): header bar, meteor area, stats bar.
- Right column (`.control-col`, `flex: 0 0 200px`): answer input display + keypad.
- The 200 px right-column width comfortably fits the keypad at the 48 × 48 px minimum
  button size.  The meteor area expands to fill all remaining width.

**Safe-area insets** (notched phones, home-bar indicators in landscape):
- Apply `padding: env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)`
  to `body`.  The `env()` function is a no-op on devices without notches.

**Font sizes**:
- `:root { font-size: 16px }` (default).
- `@media (max-width: 400px) { :root { font-size: 13px } }` (narrow phone portrait).
- `@media (orientation: landscape) and (max-height: 500px) { :root { font-size: 12px } }`
  (landscape phone with limited height).
- All component sizes in `em` units relative to `:root`.

**Keypad tap targets**: minimum `48 × 48 px` per button regardless of viewport size.

---

## Phase A1.2 — HTML/JS/CSS Implementation (Activity 1)

### Goal

Implement `main/dyn_activities/space_math.html` as a single self-contained HTML5 file
following all rules from Phase A1.1 and the esport-fi32 dynamic activity interface contract.

### Inputs

- Phase A1.1 (this document).
- `main/dyn_activities/dyn_activity1.html` — reference implementation of the credit-claim
  POST flow (URL param parsing, `formatHMS()`, `showBanner()`, claim POST with all fields).
- `docs/1-specification.md` §6.9 (credit API), §6.10 (dynamic activities URL contract).

### File

`main/dyn_activities/space_math.html`

### Required URL Query Parameters

| Parameter    | Type   | Description                                              |
| ------------ | ------ | -------------------------------------------------------- |
| `pin`        | string | 8-char device PIN from `GET /api/dyn`                    |
| `device_idx` | int    | Device slot index (0–3)                                  |
| `act_id`     | int    | Activity ID                                              |
| `credits_s`  | int    | Reference credit in seconds (`activity.credit_s`)        |
| `time_limit_s` | int  | Activity time limit in seconds (`activity.time_limit_s`) |
| `token`      | string | One-time nonce from `GET /api/dyn`                       |

If any required parameter is absent, zero (for numeric params), or fails to parse, the
game must show the `ERROR` screen immediately before any game logic runs.

### Structural Requirements

1. **Single file, no external resources.**  All CSS, JS, and graphics are inline.
2. **File size target < 40 KB** (uncompressed, unminified source).  Hard maximum: 80 KB.
   See [Flash Storage Constraints](#flash-storage-constraints).
3. **`<meta charset="UTF-8">` and `<meta name="viewport" ...>`** present (exact form
   specified in Phase A1.1 §Responsiveness and Orientation).
4. **`<title>Space Math Meteors</title>`**.
5. **CSS reset**: `box-sizing: border-box` on `*`; `margin: 0; padding: 0` on `body`.
6. **Dark space theme**: `background: #0a0a2e` on body.
7. **No `alert()`, `confirm()`, or `prompt()`** calls.
8. **No `eval()` or `innerHTML` injection of user-supplied data.**  Only game-generated
   strings (numbers, operator symbols) are inserted via `textContent` or template literals
   with numeric data only.
9. **`var` is forbidden** — use `const` and `let` throughout (ES6 minimum target).
10. **Game loop**: driven by `requestAnimationFrame` for meteor movement; a separate
    `setInterval(1000)` decrements the countdown timer and checks win/lose conditions.

### JavaScript Architecture

```
┌─ Constants (SECS_PER_PROBLEM, EARN_FRACTION, MAX_METEORS, speed tiers)
├─ Constants (SECS_PER_PROBLEM, EARN_FRACTION, MAX_METEORS, speed tiers)
├─ State variables
│   ├─ pin, deviceIdx, actId, creditsS, timeLimitS, token  (from URL params)
│   ├─ selectedTypes[]  ('mul' | 'add' | 'sub' — set from SETUP checkboxes)
│   ├─ targetProblems, solved, wrong, elapsedS
│   ├─ gameState: 'loading' | 'setup' | 'playing' | 'end'
│   ├─ meteors[]  (array of active meteor objects)
│   └─ activeMeteorId  (id of the currently-answered meteor)
├─ Problem generator
│   └─ generateProblem() → picks uniformly from selectedTypes[]
├─ Meteor lifecycle
│   ├─ spawnMeteor(problem)
│   ├─ updateMeteors(dt)   ← called from RAF loop
│   ├─ destroyMeteor(id, correct)
│   └─ meteorHitGround(id)
├─ Input handling
│   ├─ onKeypadDigit(d)
│   ├─ onKeypadBackspace()
│   ├─ onKeypadSubmit()
│   └─ onKeyboardInput(event)
├─ Timer
│   └─ onTick()  ← called by setInterval
├─ Screen renderers
│   ├─ renderSetup()     ← SETUP state; checkboxes + Start Game! button
│   ├─ renderPlaying()   ← builds the game HTML once; meteors updated by RAF
│   ├─ renderEnd(win)    ← replaces screen content
│   └─ renderError(msg)
└─ Credit claim
    └─ claimCredits()    ← same flow as dyn_activity1.html
```

### CSS Architecture

```
:root           → colour variables (--bg, --meteor-fill, --active-glow, --win-gold, ...)
body            → dark bg; portrait: flex column; landscape: flex row (orientation query)
                  safe-area-inset padding applied
.game-col       → flex: 1 1 0; header + game-area + stats; full-width in portrait
.control-col    → 200 px fixed width in landscape; full-width at bottom in portrait
.header-bar     → flex row; timer, progress bar, title
.game-area      → position: relative; overflow: hidden; flex-grow: 1
.star-layer     → position: absolute; full size; animated background dots
.meteor         → position: absolute; CSS oval; transition: top linear
.meteor.active  → pulsing box-shadow animation
.meteor.wrong   → red flash animation
.explode        → CSS keyframe burst (opacity + scale)
.stats-bar      → flex row
.input-row      → flex; answer display + submit button
.keypad         → CSS grid 3×4
.keypad-btn     → large tap targets (min 48×48 px)
.setup-screen   → centred flex column; full-viewport height; space theme background
.setup-opts     → flex column; checkbox+label rows
.setup-opt      → flex row; min 48 px tap target via checkbox sizing
.start-btn      → full-width styled button; disabled state reduces opacity
.end-screen     → centred; star-burst background animation on WIN
```

### Acceptance Criteria

- [ ] File is a valid HTML5 document with no external resource references.
- [ ] File size ≤ 40 KB uncompressed (target); must not exceed 80 KB under any circumstances.
- [ ] Source code is human-readable; no minification, no obfuscation.
- [ ] Missing/zero URL params → ERROR screen shown; game does not start.
- [ ] Valid URL params → SETUP screen shown before any game logic runs.
- [ ] SETUP screen displays three checkboxes: **Table multiplication**, **Addition**, **Subtraction** — all checked by default.
- [ ] **Start Game!** button is **disabled** when no checkbox is checked.
- [ ] **Start Game!** button becomes enabled as soon as at least one checkbox is checked.
- [ ] Pressing **Start Game!** with a valid selection transitions to PLAYING; `selectedTypes[]` contains only the chosen types.
- [ ] Only problem types present in `selectedTypes[]` are generated during gameplay.
- [ ] `targetProblems = floor(time_limit_s * 0.70 / 5)`, minimum 3.
- [ ] On-screen keypad digits append to the answer display; Backspace removes last digit;
      Enter/OK submits.
- [ ] Physical keyboard (digits, Enter, Backspace) also works for answer entry.
- [ ] Answer `maxlength` prevents entry longer than 2 digits.
- [ ] Correct answer → meteor destroyed with orange explosion animation.
- [ ] Wrong answer → meteor flashes red; timer loses 1 second.
- [ ] Meteor reaches bottom → purple explosion; no life lost; `solved` count unchanged.
- [ ] Tapping/clicking a meteor makes it the active target; answer input is cleared.
- [ ] Maximum 3 meteors on screen simultaneously.
- [ ] Meteor fall speed increases in 4 tiers as described in Phase A1.1.
- [ ] Countdown timer decrements every second and is displayed as `MM:SS`.
- [ ] Progress bar reflects `solved / targetProblems` (capped at 100%).
- [ ] When `solved >= targetProblems` before timer reaches 0 → WIN screen shown
      immediately; bonus count displayed.
- [ ] When timer reaches 0 → GAME OVER screen shown; partial credit displayed.
- [ ] WIN screen shows: solved count, target count, bonus count (if any), credits earned
      formatted as h:mm:ss.
- [ ] GAME OVER screen shows: solved count, target count, credits earned as h:mm:ss.
- [ ] "Claim Credits" button sends `POST /api/activities/credit` with correct fields
      including `completion_time_s = elapsedS`.
- [ ] HTTP 200 response → green success banner; claim button stays disabled.
- [ ] HTTP 403 → red banner (non-retriable); button stays disabled.
- [ ] HTTP 429 → orange banner (non-retriable); button stays disabled.
- [ ] Other HTTP error or network error → red banner; button re-enabled.
- [ ] Star-field background animates continuously during play.
- [ ] Active meteor highlighted with pulsing glow.
- [ ] No `var`, no `eval()`, no `alert()/confirm()`, no external resources.
- [ ] `<meta name="viewport">` includes `maximum-scale=1.0` and `user-scalable=no`.
- [ ] In portrait orientation: keypad panel appears below the meteor area.
- [ ] In landscape orientation: keypad occupies the right 200 px column; meteor area
      expands to fill all remaining width.
- [ ] No horizontal scrollbar at 320 px wide (portrait) or at 568 × 320 px (landscape phone).
- [ ] `env(safe-area-inset-*)` padding applied to `body`.
- [ ] Meteor play field uses `100dvh` (with `100vh` fallback) for height calculations.

### Required `POST /api/activities/credit` Fields

```javascript
{
  device_idx:       deviceIdx,    // int
  act_id:           actId,        // int
  credits_s:        submitCredit, // Math.floor(creditsS * solved / targetProblems), capped at creditsS
  completion_time_s: elapsedS,    // seconds elapsed since game start
  pin:              pin,          // string from URL
  token:            token         // string from URL
}
```

---

## Phase A1.3 — Integration & Verification (Activity 1)

### Goal

Register `space_math.html` as a dynamic activity in the firmware, wire it to a
test device, and verify the complete play-through flow end-to-end.

### Inputs

- Phase A1.2 output: `main/dyn_activities/space_math.html`.
- Existing Feature 8 infrastructure (Phases 8.1–8.5 must be complete).

### Tasks

1. **Build**: run `idf.py reconfigure && idf.py build`.  The CMake glob picks up
   `space_math.html` automatically; `g_dyn_act_count` increments by 1.

2. **Create activity via admin UI**:
   - Navigate to `/activities/manage`.
   - Tick "Dynamic activity".
   - Select `space_math` from the combobox.
   - Set **Credit** to `0:10:00` (600 s) and **Time Limit** to `0:02:00` (120 s),
     **Daily Limit** to `2`.
   - Submit → HTTP 303 redirect.

3. **Assign to device 0** via the same manage page.

4. **Kid workflow test**:
   - Navigate to `/dyn` from device 0's browser.
   - Verify device 0's nickname and the `space_math` button appear.
   - Click the button; confirm the URL contains `pin`, `device_idx=0`, `act_id`,
     `credits_s=600`, `time_limit_s=120`, `token`.
   - Game loads; verify `targetProblems = floor(120 * 0.70 / 5) = 16`.
   - Play to WIN (solve ≥ 16 problems before 2 min expire).
   - Claim credits; verify green success banner and `new_counter_hms`.

5. **Repeat for GAME OVER path** (let timer expire at < 16 solved).

6. **Replay attack test**: after claiming, attempt to re-submit the same POST manually
   (same `token`); server must return HTTP 403.

7. **Daily limit test**: claim twice (daily limit = 2); third attempt → HTTP 429.

8. **Tampered `time_limit_s` test**: manually load the URL with `time_limit_s=5`;
   verify `targetProblems` clamps to minimum 3; game is playable; credit cap still applies.

9. **Responsiveness test**: load game on a 320 px viewport (browser DevTools mobile
   emulation); keypad, meteors, and timer must all be visible without horizontal scroll.

10. **Build artefact check**: `g_dyn_act_registry` must include an entry with
    `p_name == "space_math"`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings.
- [ ] `g_dyn_act_count` increases by 1 compared to pre-game build.
- [ ] `GET /dyn_activities/space_math` returns HTTP 200 with `Content-Type: text/html`.
- [ ] `GET /dyn_activities/space_math.html` returns HTTP 200 (`.html` extension handled).
- [ ] `/dyn` page shows `space_math` button for registered device.
- [ ] Launch URL contains all 6 required parameters including `time_limit_s`.
- [ ] WIN flow: `POST /api/activities/credit` returns 200; counter incremented correctly.
- [ ] GAME OVER flow: proportional credits posted and accepted.
- [ ] Replay of consumed token → HTTP 403.
- [ ] Third daily claim → HTTP 429 banner shown in game.
- [ ] `time_limit_s=5` → `targetProblems == 3` (minimum clamp enforced).
- [ ] No regression in existing dynamic activity tests (Phases 8.1–8.5 criteria still pass).

---

## Activity 2 — Clown Fish Bubble Burst

### Concept

The player helps **Nemo**, a cute clown fish, defend the reef from math bubbles rising
from the sea floor.  Each bubble carries a math problem that must be solved before the
bubble floats off the top of the screen.

- Answering correctly **pops** the bubble with a sparkling white splash animation (CSS keyframe).
- A wrong answer wobbles the bubble and flashes it red, deducting 1 second from the
  countdown timer as a penalty.
- If a bubble reaches the top without a correct answer it drifts away silently; no life
  is lost — the problem is simply missed (counts as unsolved).

The clown fish character is displayed as a cute inline SVG at the bottom-centre of the
play area, gently bobbing.  On a correct answer it does a happy jump; on a wrong answer
it briefly shakes.

Engagement hooks:
- Animated CSS/SVG bubbles float upward at varying speeds (progressively faster as
  problems are solved correctly).
- An animated underwater background: deep-blue ocean gradient, swaying seaweed, and
  small rising background bubbles (pure CSS).
- A clown fish character (orange/white/black inline SVG) with idle swim, happy-jump,
  and wrong-shake CSS animations.
- A visible countdown bar plus a large numeric timer.
- Celebratory "AMAZING! You helped Nemo!" screen with a reef-glow animation on WIN.
- "TIME'S UP!" screen with proportional credit display on GAME OVER.
- Sound is NOT used (avoids permission issues and distraction in shared spaces).

Size budget: target < 40 KB, hard maximum 80 KB (uncompressed HTML file).
See [Flash Storage Constraints](#flash-storage-constraints).  All assets are CSS/SVG — no
binary images or external resources.

---

### Math Problem Rules

Identical to Activity 1:

| Type           | Operand ranges                                 | Result constraint | Example         |
| -------------- | ---------------------------------------------- | ----------------- | --------------- |
| Addition       | Both operands 1–49                             | Sum ≤ 99          | `23 + 41 = ?`   |
| Subtraction    | Minuend 10–99, subtrahend 1–(minuend-1)        | Result ≥ 1        | `54 − 17 = ?`   |
| Multiplication | Both factors 1–9 (single-digit × single-digit) | Product ≤ 81      | `7 × 6 = ?`     |

- No division.
- All answers are positive integers.
- Problems are generated pseudo-randomly on the client side using `Math.random()`.  The
  type is chosen uniformly at random from the three categories.
- A given problem is never repeated within the same game session (de-dup buffer of the
  last 10 problems).

---

### Timing & Scoring Model

Identical to Activity 1:

| Constant           | Value | Rationale          |
| ------------------ | ----- | ------------------ |
| `SECS_PER_PROBLEM` | 5     | Same as Activity 1 |
| `EARN_FRACTION`    | 0.70  | Same as Activity 1 |

```
targetProblems = Math.floor(time_limit_s * EARN_FRACTION / SECS_PER_PROBLEM)
```

Minimum `targetProblems` = 3.  Bonus problems continue after the target is reached.

---

### Credit Calculation

Identical to Activity 1:

```
solved       = number of bubbles popped correctly
earnedCredit = Math.floor(credit_s * solved / targetProblems)
submitCredit = Math.min(earnedCredit, credit_s)   // server will cap anyway
```

---

### URL Parameters & API Integration

Identical to Activity 1 — same six URL parameters (`pin`, `device_idx`, `act_id`,
`credits_s`, `time_limit_s`, `token`), same `POST /api/activities/credit` contract,
and same response handling.

Launch URL:

```
/dyn_activities/fish_math?pin=<PIN>&device_idx=<N>&act_id=<ID>&credits_s=<CREDIT_S>&time_limit_s=<N>&token=<TOKEN>
```

---

### Security Considerations

Identical to Activity 1 — same API contract, same security properties, same reasoning
about `time_limit_s` being informational only.

---

## Phase A2.1 — Game Design & UI Specification (Activity 2)

### Goal

Define the full visual layout, animation behaviour, screen states, and input method for
`fish_math.html` so that Phase A2.2 can proceed without ambiguity.

### Screen States

| State     | Trigger                                       | Description                                               |
| --------- | --------------------------------------------- | --------------------------------------------------------- |
| `LOADING` | Page load                                     | Validates URL params; transitions to `SETUP` or `ERROR`   |
| `ERROR`   | Missing/invalid URL params                    | Shows error message + back link                           |
| `SETUP`   | After `LOADING` succeeds                      | Problem-type selection screen; player chooses which operation types to practise, then presses **Start Game!** |
| `PLAYING` | Player presses **Start Game!**                | Main game loop                                            |
| `END`     | Timer reaches 0 OR `solved >= targetProblems` | WIN or GAME OVER result; credit claim auto-triggered      |

Same single `<div id="screen">` swap pattern as Activity 1.

### SETUP State Layout

Identical structure to Activity 1's SETUP state (see Phase A1.1), with the underwater
theme applied: ocean-gradient background, `#7dd4f0` subtitle text, and `#1db8d6` checkbox
accent colour and button colour.

```
┌──────────────────────────────┐
│  🐠  Fish Math               │
│                              │
│  Choose which types of       │
│  problems to include:        │
│                              │
│  ☑  Table multiplication     │
│  ☑  Addition                 │
│  ☑  Subtraction              │
│                              │
│  [ Start Game! ]  ← disabled │
│                  if none ✓   │
└──────────────────────────────┘
```

### PLAYING State Layout

The game uses the same **responsive two-layout design** as Activity 1.

**Portrait layout** (default — phone or tablet upright):

```
┌─────────────────────────────────────┐
│ 🐠 Fish Math  ⏱ 01:52  [====    ]   │  ← header bar
├─────────────────────────────────────┤
│  [bubble 8×6=?]   [bubble 27+34=?]  │  ← bubbles floating UP
│                                     │
│       [bubble 71-29=?]              │  ← (newer bubbles spawn below)
│                                     │
│       ><(((°>  (Nemo)               │  ← decorative fish at bottom
├─────────────────────────────────────┤
│  Popped: 5 / 16       Wrong: 2      │  ← stats bar
├─────────────────────────────────────┤
│       [  _ _ _  ]  [✓ OK]           │  ← answer input + submit
│   [ 1 ][ 2 ][ 3 ]                   │
│   [ 4 ][ 5 ][ 6 ]                   │  ← on-screen keypad
│   [ 7 ][ 8 ][ 9 ]                   │
│        [ 0 ][ ← ]                   │
└─────────────────────────────────────┘
```

**Landscape layout** (`@media (orientation: landscape)` — two-column split):

Identical structure to Activity 1 — keypad occupies the right 200 px column; the game
area fills all remaining width.

**Active bubble selection**: auto-selects the bubble closest to escaping (smallest
`topPx` — nearest to the top of the screen).  The player can tap or click any bubble to
make it the active target; switching clears the answer input.

**Answer input**: identical to Activity 1 — `<input type="text" inputmode="numeric"
pattern="[0-9]*" maxlength="2">`, on-screen keypad + physical keyboard.

### Bubble Visual Specification

Each bubble is a `<div>` absolutely positioned inside the game area:
- Shape: CSS circle (`border-radius: 50%`), `70 × 70 px`.
- Fill: translucent ocean-blue with a glossy highlight:
  `background: radial-gradient(circle at 30% 30%, rgba(180,240,255,0.85), rgba(20,100,180,0.65))`.
- Border: `2px solid rgba(150,230,255,0.7)` (translucent light-blue rim).
- Text: problem string centred in bold white, `font-size: 1em`,
  `text-shadow: 0 1px 3px rgba(0,0,80,0.8)`.
- Movement: bubbles move **upward** — `topPx` decreases each RAF frame.
- Active bubble: `box-shadow: 0 0 14px 5px rgba(255,154,60,0.9)` pulsing warm-orange glow
  (clown fish colours).
- Correct pop: CSS `@keyframes bubblePop` — scale up + fade out (white radial burst,
  400 ms); bubble removed from DOM.
- Wrong answer: CSS `@keyframes bubbleWobble` — horizontal shake + red border flash,
  300 ms.
- Escapes top: CSS `@keyframes bubbleEscape` — fade-out with slight upward drift, 300 ms.

### Bubble Spawn & Speed Rules

- Maximum 3 bubbles on screen simultaneously.
- A new bubble is spawned:
  - At game start: first bubble immediately, second after 4 000 ms, third after 8 000 ms.
  - After a bubble is popped or escapes: next bubble spawns immediately; multiple empty
    slots refilled with 800 ms between each spawn.
- Bubbles spawn at the **bottom** of the game area (`topPx = gameAreaH + 10`), rising
  upward through the seaweed and past the fish character.
- **Escape check**: `topPx <= −70` (bubble's top edge is 70 px above the game area).
- Rise duration (same 4-tier system as Activity 1):
  - Problems 1–5: 12 s per bubble.
  - Problems 6–10: 11 s per bubble.
  - Problems 11–20: 10 s per bubble.
  - Problems 21+: 9 s per bubble (minimum).
- Horizontal start position: random `left` between 5 % and 72 % of the game area width,
  with a minimum 24 % separation from any other bubble already on screen (same rule as
  Activity 1).

### Clown Fish Character

The fish is an inline SVG displayed at the bottom-centre of `.game-area`:
- Positioned absolutely: `bottom: 18px; left: 50%; transform: translateX(-50%)`.
- Rendered size: `88 × 52 px` (`viewBox="0 0 110 65"`).
- Visual design (all SVG primitives, no external assets):
  - Orange body: `<ellipse>` `fill="#ff6a00"`.
  - Tail fin: `<path>` triangle pointing right, `fill="#e65800"`.
  - Three white bands: `<rect rx="7">` elements clipped to the body with `<clipPath id="bc">`;
    black outlines `stroke="#1a0000"`.
  - Body outline redrawn on top of the clip group: `fill="none" stroke="#1a0000"`.
  - Pectoral fin: `<ellipse>` below the body, `fill="#ff8c3a"`, slightly rotated.
  - Dorsal fin: `<path>` curved arc above the body, `stroke="#e65800" stroke-width="5"`.
  - Eye: white `<circle>` (sclera), green iris `<circle>`, black pupil `<circle>`, white
    specular highlight `<circle>`.
  - Smile: `<path>` with a quadratic bézier curve below the eye.
- CSS animations on `.fish-char`:
  - **Idle**: `@keyframes swim` — `translateY(0)` ↔ `translateY(-6px)`, 2 s ease-in-out
    infinite.
  - **Happy** (correct answer): `@keyframes fishHappy` — jump to `translateY(-14px)
    scale(1.12)` and back, 400 ms; JS removes `happy` class after 420 ms to restore idle.
  - **Wrong** (wrong answer): `@keyframes fishWrong` — `translateX ±8 px` shake, 350 ms;
    JS removes `wrong` class after 360 ms to restore idle.

### Background

Underwater ocean scene (pure CSS, no images):
- Body background: `linear-gradient(180deg, #001428 0%, #003d5c 60%, #004830 100%)`.
- `.game-area` inherits the same gradient.
- 3 background rising-bubble layers (`.bub-layer`): `radial-gradient(circle, rgba(...) 1.5px,
  transparent 1.5px)` repeating; each layer animated upward at different speeds (`@keyframes
  rise1/2/3`, 8 s / 13 s / 18 s) replacing the `.star-layer` concept from Activity 1.
- Sea floor: `.sea-floor` — `position: absolute; bottom: 0; height: 18px`;
  `background: linear-gradient(#3a2e14, #6a5428)`;
  `border-radius: 50% 50% 0 0 / 14px 14px 0 0`.
- Seaweed: 5 `.seaweed` `<div>`s at varied `left` positions (8 %, 17 %, 50 %, 75 %,
  84 %), heights 24–36 px; `background: #1a6b3a`; `border-radius: 4px 4px 0 0`;
  `transform-origin: bottom center`; each sways with `@keyframes sway` (±8 °, 2.7–3.4 s,
  `animation-direction: alternate`, staggered `animation-delay`).

### WIN State Layout

```
┌──────────────────────────────┐
│   🐠  AMAZING!               │
│                              │
│  You helped Nemo! 🐠          │
│  You popped 18 / 16 bubbles  │
│  (+2 BONUS bubbles! 🏆)       │
│                              │
│  Credits earned: 0:10:00     │
│                              │
│  Saving your credits…        │
│  ← Back to Games             │
└──────────────────────────────┘
```

WIN title colour: `#ff9a3c` (clown fish orange) with matching `text-shadow` glow.
WIN background: `@keyframes reefBurst` — subtle animated gradient pulse.

### GAME OVER State Layout

```
┌──────────────────────────────┐
│   ⏱  TIME'S UP!             │
│                              │
│  You popped 11 / 16 bubbles  │
│  Credits earned: 0:06:53     │
│                              │
│  Saving your credits…        │
│  ← Back to Games             │
└──────────────────────────────┘
```

### Responsiveness and Orientation

Identical to Activity 1 — same breakpoints, same safe-area-inset handling, same landscape
two-column layout (keypad right 200 px column), same font-size media queries, same
`100dvh`/`100vh` fallback pattern.

---

## Phase A2.2 — HTML/JS/CSS Implementation (Activity 2)

### Goal

Implement `main/dyn_activities/fish_math.html` as a single self-contained HTML5 file
following all rules from Phase A2.1 and the esport-fi32 dynamic activity interface contract.

### Inputs

- Phase A2.1 (this document).
- `main/dyn_activities/space_math.html` — reference implementation for code structure,
  input handling, timer, credit-claim POST flow, and screen swap pattern.
- `docs/1-specification.md` §6.9 (credit API), §6.10 (dynamic activities URL contract).

### File

`main/dyn_activities/fish_math.html`

### Required URL Query Parameters

| Parameter      | Type   | Description                                              |
| -------------- | ------ | -------------------------------------------------------- |
| `pin`          | string | 8-char device PIN from `GET /api/dyn`                    |
| `device_idx`   | int    | Device slot index (0–3)                                  |
| `act_id`       | int    | Activity ID                                              |
| `credits_s`    | int    | Reference credit in seconds (`activity.credit_s`)        |
| `time_limit_s` | int    | Activity time limit in seconds (`activity.time_limit_s`) |
| `token`        | string | One-time nonce from `GET /api/dyn`                       |

If any required parameter is absent, zero (for numeric params), or fails to parse, the
game must show the `ERROR` screen immediately before any game logic runs.

### Structural Requirements

1–10: all identical to Activity 1, except:
- **`<title>Fish Math: Bubble Reef</title>`**.
- **Underwater theme**: body background `linear-gradient(180deg, #001428, #003d5c)`.
- **Bubbles move upward**: `topPx` decreases each RAF frame; escape check `topPx <= −70`.
- **Active bubble auto-selection**: bubble with the minimum `topPx` (closest to escaping).
- **Inline clown fish SVG** character in the game area (see Phase A2.1).
- **Stat label** uses "Popped" instead of "Solved" for thematic consistency.

### JavaScript Architecture

```
┌─ Constants (SECS_PER_PROBLEM, EARN_FRACTION, MAX_BUBBLES, speed tiers)
├─ State variables
│   ├─ pin, deviceIdx, actId, creditsS, timeLimitS, token  (from URL params)
│   ├─ selectedTypes[]  ('mul' | 'add' | 'sub' — set from SETUP checkboxes)
│   ├─ targetProblems, solved, wrong, elapsedS
│   ├─ gameState: 'loading' | 'setup' | 'playing' | 'end'
│   ├─ bubbles[]  (array of active bubble objects)
│   ├─ activeBubbleId  (id of the currently-targeted bubble)
│   └─ fishHappyTimer, fishWrongTimer  (timeout handles for fish animation reset)
├─ Problem generator
│   └─ generateProblem()  (picks uniformly from selectedTypes[]; otherwise identical to Activity 1)
├─ Bubble lifecycle
│   ├─ spawnBubble()
│   ├─ rafLoop(now)          ← moves bubbles upward; detects escape at topPx <= -70
│   ├─ destroyBubble(id, correct)
│   └─ spawnBubblesIfNeeded()
├─ Fish reactions
│   ├─ triggerFishHappy()   ← adds .happy class; auto-removes after 420 ms
│   └─ triggerFishWrong()   ← adds .wrong class; auto-removes after 360 ms
├─ Input handling  (identical to Activity 1)
├─ Timer  (identical to Activity 1)
├─ Screen renderers
│   ├─ renderSetup()        ← SETUP state; checkboxes + Start Game! button (ocean theme)
│   ├─ renderPlaying()      ← builds game HTML once; injects FISH_SVG constant
│   ├─ renderEnd(win)       ← same layout/style as Activity 1 END screen
│   └─ renderError(msg)
└─ Credit claim  (identical to Activity 1)
```

### CSS Architecture

```
:root           → colour variables (--bg1 #001428, --bg2 #003d5c, --bubble-fill,
                  --active-glow #ff9a3c, --win-gold #ff9a3c, ...)
body            → ocean gradient; portrait: flex column; landscape: flex row
.game-col       → flex: 1 1 0; same structure as Activity 1
.control-col    → same as Activity 1
.header-bar     → same structure as Activity 1
.game-area      → position: relative; overflow: hidden; ocean gradient background
.bub-layer      → rising background dots (replaces .star-layer)
.sea-floor      → absolute bottom strip; sandy gradient
.seaweed        → 5 instances; sway animation
.fish-char      → absolute bottom-centre; idle swim animation
.bubble         → position: absolute; CSS circle; translucent teal-blue
.bubble.active  → pulsing orange glow animation
.bubble.wrong   → horizontal wobble/shake animation
.pop            → CSS keyframe burst (white radial → transparent, scale+fade)
.pop.escape     → CSS fade-up (opacity + translateY)
.stats-bar      → same as Activity 1
.input-row      → same as Activity 1
.keypad         → same as Activity 1
.setup-screen   → centred flex column; full-viewport height; ocean gradient background
.setup-opts     → flex column; checkbox+label rows
.setup-opt      → flex row; min 48 px tap target via checkbox sizing
.start-btn      → full-width styled button; disabled state reduces opacity
.end-screen     → centred; reef-burst glow animation on WIN
```

### Acceptance Criteria

- [ ] File is a valid HTML5 document with no external resource references.
- [ ] File size ≤ 40 KB uncompressed (target); must not exceed 80 KB under any circumstances.
- [ ] Source code is human-readable; no minification, no obfuscation.
- [ ] `<title>` is "Fish Math: Bubble Reef".
- [ ] Missing/zero URL params → ERROR screen shown; game does not start.
- [ ] Valid URL params → SETUP screen shown before any game logic runs.
- [ ] SETUP screen displays three checkboxes: **Table multiplication**, **Addition**, **Subtraction** — all checked by default.
- [ ] **Start Game!** button is **disabled** when no checkbox is checked.
- [ ] **Start Game!** button becomes enabled as soon as at least one checkbox is checked.
- [ ] Pressing **Start Game!** with a valid selection transitions to PLAYING; `selectedTypes[]` contains only the chosen types.
- [ ] Only problem types present in `selectedTypes[]` are generated during gameplay.
- [ ] `targetProblems = floor(time_limit_s * 0.70 / 5)`, minimum 3.
- [ ] Bubbles move **upward**; escape check at `topPx <= −70`.
- [ ] Active bubble = bubble with minimum `topPx` (closest to escaping); auto-updated.
- [ ] Tapping/clicking a bubble makes it the active target; answer input is cleared.
- [ ] Maximum 3 bubbles on screen simultaneously.
- [ ] On-screen keypad digits append; Backspace removes last digit; Enter/OK submits.
- [ ] Physical keyboard (digits, Enter, Backspace) also works for answer entry.
- [ ] Answer `maxlength` prevents entry longer than 2 digits.
- [ ] Correct answer → white splash pop animation (scale + fade); bubble removed from DOM.
- [ ] Correct answer → clown fish happy-jump animation triggered.
- [ ] Wrong answer → bubble wobbles red; timer loses 1 second.
- [ ] Wrong answer → clown fish wrong-shake animation triggered.
- [ ] Bubble escapes top → fade-up animation; no life lost; `solved` count unchanged.
- [ ] Clown fish SVG visible at bottom-centre of game area during PLAYING state.
- [ ] Clown fish idle swim animation runs continuously during PLAYING state.
- [ ] Seaweed sway animation visible at bottom of game area.
- [ ] Background rising-bubble layers animate continuously.
- [ ] Countdown timer decrements every second and is displayed as `MM:SS`.
- [ ] Progress bar reflects `solved / targetProblems` (capped at 100 %).
- [ ] `solved >= targetProblems` before timer → WIN screen immediately; bonus count shown.
- [ ] Timer reaches 0 → GAME OVER screen; partial credit shown.
- [ ] WIN screen: title "AMAZING!" in orange-gold; popped/target/bonus counts; credits.
- [ ] GAME OVER screen: "TIME'S UP!"; popped/target counts; credits formatted as h:mm:ss.
- [ ] Credits submitted automatically on END screen; "Saving your credits…" shown.
- [ ] HTTP 200 → green banner "Credits added! Counter: …"; no retry button.
- [ ] HTTP 403 → red non-retriable banner; button stays disabled.
- [ ] HTTP 429 → orange non-retriable banner; button stays disabled.
- [ ] Other HTTP or network error → red banner with **Try again** button (retriable).
- [ ] `<meta name="viewport">` includes `maximum-scale=1.0` and `user-scalable=no`.
- [ ] Portrait: keypad panel appears below the game area.
- [ ] Landscape: keypad occupies the right 200 px column; game area expands to fill remaining width.
- [ ] No horizontal scrollbar at 320 px wide (portrait) or 568 × 320 px (landscape phone).
- [ ] `env(safe-area-inset-*)` padding applied to `body`.
- [ ] No `var`, no `eval()`, no `alert()/confirm()`, no external resources.

### Required `POST /api/activities/credit` Fields

```javascript
{
  device_idx:        deviceIdx,    // int
  act_id:            actId,        // int
  credits_s:         submitCredit, // Math.floor(creditsS * solved / targetProblems), capped at creditsS
  completion_time_s: elapsedS,     // seconds elapsed since game start
  pin:               pin,          // string from URL
  token:             token         // string from URL
}
```

---

## Phase A2.3 — Integration & Verification (Activity 2)

### Goal

Register `fish_math.html` as a dynamic activity, wire it to a test device, and verify
the complete play-through flow end-to-end.

### Inputs

- Phase A2.2 output: `main/dyn_activities/fish_math.html`.
- Existing Feature 8 infrastructure (Phases 8.1–8.5 must be complete).

### Tasks

1. **Build**: run `idf.py reconfigure && idf.py build`.  The CMake glob picks up
   `fish_math.html` automatically; `g_dyn_act_count` increments by 1.

2. **Create activity via admin UI**:
   - Navigate to `/activities/manage`.
   - Tick "Dynamic activity".
   - Select `fish_math` from the combobox.
   - Set **Credit** to `0:10:00` (600 s) and **Time Limit** to `0:02:00` (120 s),
     **Daily Limit** to `2`.
   - Submit → HTTP 303 redirect.

3. **Assign to device** via the same manage page.

4. **Kid workflow test**:
   - Navigate to `/dyn` from the assigned device's browser.
   - Verify device nickname and the `fish_math` button appear.
   - Click the button; confirm URL contains `pin`, `device_idx`, `act_id`,
     `credits_s=600`, `time_limit_s=120`, `token`.
   - Game loads; verify `targetProblems = floor(120 × 0.70 / 5) = 16`.
   - Play to WIN (pop ≥ 16 bubbles before 2 min expire).
   - Verify green "Credits added!" banner and `new_counter_hms`.

5. **Repeat for GAME OVER path** (let timer expire with < 16 bubbles popped).

6. **Replay attack test**: after claiming, re-submit the same POST (same `token`);
   server must return HTTP 403.

7. **Daily limit test**: claim twice (daily limit = 2); third attempt → HTTP 429 banner.

8. **Tampered `time_limit_s` test**: manually load URL with `time_limit_s=5`; verify
   `targetProblems` clamps to minimum 3; game is playable; credit cap still applies.

9. **Responsiveness test**: load game on a 320 px viewport (browser DevTools mobile
   emulation); keypad, bubbles, fish character, and timer must all be visible without
   horizontal scroll.

10. **Build artefact check**: `g_dyn_act_registry` must include an entry with
    `p_name == "fish_math"`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings.
- [ ] `g_dyn_act_count` increases by 1 compared to pre-game build.
- [ ] `GET /dyn_activities/fish_math` returns HTTP 200 with `Content-Type: text/html`.
- [ ] `GET /dyn_activities/fish_math.html` returns HTTP 200 (`.html` extension handled).
- [ ] `/dyn` page shows `fish_math` button for registered device.
- [ ] Launch URL contains all 6 required parameters including `time_limit_s`.
- [ ] WIN flow: `POST /api/activities/credit` returns 200; counter incremented correctly.
- [ ] GAME OVER flow: proportional credits posted and accepted.
- [ ] Replay of consumed token → HTTP 403.
- [ ] Third daily claim → HTTP 429 banner shown in game.
- [ ] `time_limit_s=5` → `targetProblems == 3` (minimum clamp enforced).
- [ ] No regression in existing Activity 1 tests or Phases 8.1–8.5 criteria.

---

## Activity 3 — Rocket Asteroid Shooter

### Concept

The player pilots a **rocket/spaceship** defending against incoming asteroids.  Each round
presents a single math problem displayed in the header.  Three asteroids approach
simultaneously from the far side of the screen — one per row — each carrying a candidate
answer (1 correct + 2 plausible distractors).

The player must:
1. Move the rocket up or down between the three rows to align with the correct answer, OR
2. Fire a laser at the correct-answer asteroid before the set reaches the rocket.

**Answer selection mechanics:**
- Tapping an asteroid **directly** instantly snaps the rocket to that row and fires
  (immediate shot).
- The **▲ / ▼** direction buttons (or keyboard Arrow keys) move the rocket one row at a
  time without firing.  Movement is **clamped**: pressing ▲ at row 0 (top) or ▼ at row 2
  (bottom) has no effect — the rocket does not wrap around.
- The **FIRE** button (or **Space** key) fires a laser at the asteroid in the rocket's
  current row.
- When the asteroid set reaches the rocket's side, the asteroid in the rocket's current
  row **collides** with the rocket and is evaluated as the player's selected answer — there
  is no way for asteroids to pass the spaceship.

**Resolution:**
- **Correct answer hit or collided** → the struck asteroid explodes (orange burst);
  the remaining two asteroids instantly flee off-screen at high speed.  `solved++`.
  New problem + new set spawns immediately.
- **Wrong answer hit or collided** → the struck asteroid's row is immediately flagged;
  −1 second timer penalty; `wrong++`.  All three asteroids in the set then flee
  off-screen at high speed and a new set spawns.  Every set is resolved by exactly
  one shot or collision — there is no multi-shot within a single set.

The **direction** of play is chosen on the SETUP screen:
- **Right → Left** (default): rocket on the right, asteroids fly in from the left.
  Rocket SVG faces left (←).
- **Left → Right**: rocket on the left, asteroids fly in from the right.  Rocket SVG
  faces right (→).

This is purely cosmetic — gameplay, scoring, and controls are identical in both modes.
The layout is mirrored via a CSS class on the body element.

Engagement hooks:
- Inline SVG rocket with pulsing thruster-glow CSS animation; green flash on correct hit,
  red shake on wrong.
- Laser beam (`3px` coloured line) shoots from rocket to target (CSS `@keyframes`, 150 ms).
- Surviving asteroids flee at high speed (CSS `transform: translateX` transition) after
  a correct hit.
- Animated star-field background (identical CSS pattern to Activity 1).
- Speed tier progression: asteroids travel faster as more problems are solved.
- Celebratory "BULLSEYE! YOU WIN!" screen with star-burst animation on WIN.
- "TIME'S UP!" screen with proportional credit on GAME OVER.
- Sound is NOT used (avoids permission issues and distraction in shared spaces).

Size budget: target < 40 KB, hard maximum 80 KB (uncompressed HTML file).
See [Flash Storage Constraints](#flash-storage-constraints).  All assets are CSS/SVG — no
binary images or external resources.

---

### Math Problem Rules

Identical to Activities 1 and 2:

| Type           | Operand ranges                                 | Result constraint | Example         |
| -------------- | ---------------------------------------------- | ----------------- | --------------- |
| Addition       | Both operands 1–49                             | Sum ≤ 99          | `23 + 41 = ?`   |
| Subtraction    | Minuend 10–99, subtrahend 1–(minuend-1)        | Result ≥ 1        | `54 − 17 = ?`   |
| Multiplication | Both factors 1–9 (single-digit × single-digit) | Product ≤ 81      | `7 × 6 = ?`     |

- No division.
- All answers are positive integers.
- Problems are generated pseudo-randomly using `Math.random()`, picking uniformly from
  `selectedTypes[]`.
- De-dup buffer of the last 10 problems prevents consecutive repeats.

#### Distractor Generation

Each set of 3 asteroids contains 1 correct answer and 2 plausible distractors assigned
randomly to rows 0, 1, 2.

| Problem type | Distractor strategy |
| ------------ | ------------------- |
| Addition / Subtraction | `correct ± offset` where offset is a random integer 1–9; re-roll if result ≤ 0 or > 99 |
| Multiplication | neighbour from the times table: vary one factor by ±1 (e.g. for 6 × 7 = 42, candidates are 6 × 6 = 36 and 6 × 8 = 48) |

Rules:
- Both distractors must differ from the correct answer and from each other.
- Re-roll up to 10 times; fallback to `correct ± 10` (clamped to valid range) if still
  colliding.
- Distractors must be positive integers.

---

### Timing & Scoring Model

Identical to Activities 1 and 2:

| Constant           | Value | Rationale          |
| ------------------ | ----- | ------------------ |
| `SECS_PER_PROBLEM` | 5     | Same as Activity 1 |
| `EARN_FRACTION`    | 0.70  | Same as Activity 1 |

```
targetProblems = Math.floor(time_limit_s * EARN_FRACTION / SECS_PER_PROBLEM)
```

Minimum `targetProblems` = 3.  Bonus problems continue after the target is reached.

**Asteroid travel speed** (time for one set to travel from the far edge to the rocket):

| Problems solved correctly so far | Travel time |
| --------------------------------- | ----------- |
| 0–4                               | 8 s         |
| 5–9                               | 7 s         |
| 10–19                             | 6 s         |
| 20+                               | 5 s (minimum) |

Only one set of asteroids is active at a time.  A new set spawns immediately after the
previous set is resolved (correct hit, correct collision, or wrong collision).

---

### Credit Calculation

Identical to Activities 1 and 2:

```
solved       = number of problems answered correctly
earnedCredit = Math.floor(credit_s * solved / targetProblems)
submitCredit = Math.min(earnedCredit, credit_s)   // server will cap anyway
```

---

### URL Parameters & API Integration

Identical to Activities 1 and 2 — same six URL parameters (`pin`, `device_idx`, `act_id`,
`credits_s`, `time_limit_s`, `token`), same `POST /api/activities/credit` contract, and
same response handling.

Launch URL:

```
/dyn_activities/shoot_math?pin=<PIN>&device_idx=<N>&act_id=<ID>&credits_s=<CREDIT_S>&time_limit_s=<N>&token=<TOKEN>
```

---

### Security Considerations

Identical to Activities 1 and 2 — same API contract, same security properties, same
reasoning about `time_limit_s` being informational only.

---

## Phase A3.1 — Game Design & UI Specification (Activity 3)

### Goal

Define the full visual layout, animation behaviour, screen states, and input method for
`shoot_math.html` so that Phase A3.2 can proceed without ambiguity.

### Screen States

| State     | Trigger                                       | Description                                               |
| --------- | --------------------------------------------- | --------------------------------------------------------- |
| `LOADING` | Page load                                     | Validates URL params; transitions to `SETUP` or `ERROR`   |
| `ERROR`   | Missing/invalid URL params                    | Shows error message + back link                           |
| `SETUP`   | After `LOADING` succeeds                      | Problem-type checkboxes + direction radio; player presses **Start Game!** |
| `PLAYING` | Player presses **Start Game!**                | Main game loop                                            |
| `END`     | Timer reaches 0 OR `solved >= targetProblems` | WIN or GAME OVER result; credit claim auto-triggered      |

Same single `<div id="screen">` swap pattern as Activities 1 and 2.

### SETUP State Layout

Extends the standard Activity 1/2 SETUP screen with an additional **Direction** radio
group below the problem-type checkboxes.  The space theme (dark background `#0a0a2e`,
gold title, accent blue checkboxes/buttons) is shared with Activity 1.

```
┌──────────────────────────────────┐
│  🚀  Shoot Math                  │
│                                  │
│  Choose which types of           │
│  problems to include:            │
│                                  │
│  ☑  Table multiplication         │
│  ☑  Addition                     │
│  ☑  Subtraction                  │
│                                  │
│  Direction:                      │
│  ◉ Asteroids fly right → left    │
│  ○ Asteroids fly left → right    │
│                                  │
│  [ Start Game! ]  ← disabled     │
│                  if none ✓       │
└──────────────────────────────────┘
```

- All three checkboxes are **checked by default**.
- Direction **right → left** (rocket on right, asteroids from left) is **selected by
  default**.
- The **Start Game!** button is **disabled** when no checkbox is checked; re-enabled when
  at least one is checked.  The direction radio does not affect the disabled state.
- Pressing **Start Game!** records `selectedTypes[]` and `flyDir` (`'rtl'` | `'ltr'`)
  and transitions to `PLAYING`.

### PLAYING State Layout

**Landscape is the primary and preferred orientation** — the game is inherently
horizontal.  Portrait is supported as a secondary layout with a reduced play area.

**Landscape layout** (`@media (orientation: landscape)` — two-column, `flex-direction: row`):

```
┌──────────────────────────────────────────────────┬──────────────┐
│ 🚀 Shoot Math   ⏱ 01:52   [======    ]           │ 27 + 15 = ?  │
├──────────────────────────────────────────────────┤              │
│                                                  │   [ ▲ ]      │
│  [ast: 36] ══════════════════════════════        │              │
│                                       laser →    │  [🔥 FIRE]   │
│  [ast: 42] ══════════════════════════════  🚀   │              │
│                                                  │   [ ▼ ]      │
│  [ast: 17] ══════════════════════════════        │              │
│                                                  │ Shot: 5 / 16 │
│                                                  │ Wrong: 2     │
└──────────────────────────────────────────────────┴──────────────┘
  ←────────── game area (.game-col, flex: 1 1 0) ──→  ←── 200 px ──→
```

The right column (`.control-col`, `flex: 0 0 200px`) contains: the math problem, ▲ / ▼
direction buttons, FIRE button, and stats (Shot count, Wrong count).

For `flyDir = 'ltr'`: the body gains class `.ltr`; `flex-direction` is set to
`row-reverse`, so `.control-col` moves to the **left** side.  The rocket SVG is mirrored
via `transform: scaleX(-1)` and positioned at the left edge of the game area.  The
asteroid animation direction is reversed.

**Portrait layout** (fallback — `@media (orientation: portrait)`, `flex-direction: column`):

```
┌──────────────────────────────────────────┐
│ 🚀 Shoot Math  ⏱ 01:52  [======    ]    │  ← header
├──────────────────────────────────────────┤
│  [ast: 36] ════════════════════  🚀     │  ← active row
│  [ast: 42] ════════════════════          │
│  [ast: 17] ════════════════════          │
├──────────────────────────────────────────┤
│  Shot: 5 / 16              Wrong: 2      │  ← stats bar
├──────────────────────────────────────────┤
│  27 + 15 = ?           [🔥 FIRE]         │  ← problem + fire
│          [ ▲ Up ]   [ ▼ Down ]           │  ← move buttons
└──────────────────────────────────────────┘
```

In portrait, the control panel becomes a bottom strip below the game area (problem text +
FIRE button + ▲▼ buttons in a two-row flex layout).

### Row and Rocket Layout

The game area is divided into **3 equal horizontal rows** (`height: 33.33%` each).  Row
dividers are subtle dotted lines (`border-bottom: 1px dotted rgba(255,255,255,0.12)`).

The rocket is an inline SVG (`60 × 36 px`) positioned at the right edge (RTL) or left
edge (LTR) of the game area, vertically centred within its current row.  Row transitions
are **discrete** (instant snap, no animation) accompanied by a brief
`transform: scale(1.2)` pulse (80 ms) on the rocket to give tactile feedback.

**Active row highlight**: the row containing the rocket has a subtle background tint
(`rgba(255, 233, 74, 0.08)`) and the rocket emits a pulsing glow
(`box-shadow: 0 0 14px 4px #ffe94a`).

Row indices `0`, `1`, `2` are numbered top to bottom.  ▲ decrements the row index (wraps
from `0` to `2`); ▼ increments it (wraps from `2` to `0`).

### Rocket Visual Specification

Inline SVG spaceship — approximately 4–6 `<polygon>` / `<ellipse>` / `<rect>` elements:

- Triangular body + cockpit window + engine nozzle, pointing **left** for RTL.
  For LTR apply `transform: scaleX(-1)` on the SVG element.
- Thruster glow: `@keyframes` pulsing orange/yellow `filter: drop-shadow(...)` behind
  the engine nozzle, cycling 0.8 s.
- **Correct hit reaction**: `@keyframes` `filter: brightness(4) hue-rotate(120deg)` green
  flash (200 ms), then return to normal.
- **Wrong hit reaction**: `@keyframes` horizontal shake (±6 px, 4 cycles, 300 ms).

### Asteroid Visual Specification

Each asteroid is a `<div>` absolutely positioned within its row:

- Shape: CSS oval (`border-radius: 50%`) with a `radial-gradient` for a rocky 3D
  appearance (dark centre, lighter edge).
- Size: `90 × 55 px` baseline; scales down on narrow screens.
- Displays the **candidate answer number** centred, bold white, `1.2em`.
- Movement: CSS `animation: flyRTL / flyLTR linear` — RTL: `left` from `-110px` to
  `calc(100% - 68px)` (travels left→right toward rocket on right); LTR: `right` from
  `-110px` to `calc(100% - 68px)` (travels right→left toward rocket on left); duration
  from the speed tier table.
- **Wrong shot or collision**: the struck asteroid immediately plays
  `@keyframes astWrongHit` — a 250 ms CSS animation that brightens the asteroid,
  adds a red `drop-shadow`, and scales it up briefly (`scale(1.25)`) before
  shrinking back.  After 260 ms all three asteroids in the set flee off-screen and a
  new set spawns.  The laser colour is also red for wrong shots.
- **Correct explosion**: CSS `@keyframes` orange/yellow radial burst, 450 ms (same
  pattern as `space_math.html`); element removed from DOM after animation completes.
- **Flee animation**: on a correct hit, the two surviving asteroids play
  `transform: translateX(+150vw)` (RTL) or `translateX(-150vw)` (LTR) with
  `transition: transform 0.35s ease-in`, then are removed from DOM.
- **Collision**: detected in the RAF loop using `getBoundingClientRect()`.  The
  collision fires the moment the asteroid's **leading edge** (right edge in RTL,
  left edge in LTR) touches the rocket's **nose tip** — i.e. when
  `astRect.right >= rktRect.left` (RTL) or `astRect.left <= rktRect.right` (LTR).
  This is screen-width-independent and fires visually at first contact, not at
  animation completion.

### Laser Visual Specification

When FIRE is triggered (FIRE button, Space key, or tap-asteroid):

- A `<div class="laser">` with `height: 3px` and
  `background: linear-gradient(to left, var(--accent), transparent)` (RTL; reversed for LTR)
  spanning horizontally from the rocket's edge to the target asteroid's centre.
- `@keyframes laserShoot`: `transform: scaleX(0)` → `scaleX(1)` with
  `transform-origin: right` (RTL), completing in 150 ms.
- Element is removed immediately after 150 ms.
- If the shot is wrong, the laser colour switches to `#ff4444` for its final 50 ms
  (achieved by adding a `.wrong` class at the midpoint of the animation).

### Spawn & Timing Rules

- At game start the first set of 3 asteroids spawns immediately.
- A new set spawns as soon as the previous set is fully resolved.
- All 3 asteroids in a set are spawned simultaneously and share the same travel duration.
- Asteroid visual position is driven by a CSS `@keyframes` animation whose duration
  matches the travel time.  Collision is detected in the **RAF loop** using
  `getBoundingClientRect()` — no elapsed-time threshold is used.
- **Collision** (RAF loop fires per-frame):
  - RTL: `ast.getBoundingClientRect().right >= rktWrap.getBoundingClientRect().left`
  - LTR: `ast.getBoundingClientRect().left  <= rktWrap.getBoundingClientRect().right`
  - The asteroid in `rocketRow` is evaluated as the collision answer.
  - Correct → `solved++`; correct explosion; surviving asteroids flee; new set spawns.
  - Wrong → `wrong++`; −1 s; `astWrongHit` flash (250 ms); red rocket shake; all
    asteroids flee (260 ms delay); `solved` unchanged; new set spawns.
- Only one set is active at a time; no overlapping sets.

### Background

Identical to Activity 1: pure CSS animated star field (3 `<div class="star-layer">` with
`background-image: radial-gradient(...)` repeating dots, each at a different animation
speed).  Dark navy background (`#0a0a2e`).

### WIN State Layout

```
┌──────────────────────────────┐
│   🌟  BULLSEYE! YOU WIN!  🌟  │
│                              │
│  You solved 18 / 16          │
│  (+2 BONUS problems!)        │
│                              │
│  Credits earned: 0:10:00     │
│  (+ 2 bonus = 🏆 Champion!)  │
│                              │
│  [ Claim Credits ]           │
│  ← Back to Games             │
└──────────────────────────────┘
```

### GAME OVER State Layout

```
┌──────────────────────────────┐
│   💫  TIME'S UP!             │
│                              │
│  You solved 11 / 16          │
│  Credits earned: 0:06:53     │
│                              │
│  [ Claim Credits ]           │
│  ← Back to Games             │
└──────────────────────────────┘
```

### Responsiveness and Orientation

**Primary target**: tablet (768–1024 px) in **landscape** orientation; the game is
inherently horizontal.  Smartphone portrait (360–414 px) is a supported secondary layout.

**Viewport meta tag** (same as Activities 1 and 2 — must be exactly this):

```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
```

**Landscape layout** (`@media (orientation: landscape)` — `flex-direction: row`):
- `.game-col` (`flex: 1 1 0`): header bar + 3-row game area.
- `.control-col` (`flex: 0 0 200px`): problem text, ▲/▼ buttons, FIRE button, stats.
- `body.ltr`: `flex-direction: row-reverse` moves `.control-col` to the left side.

**Portrait layout** (default — `flex-direction: column`):
- Header at top; game area (`flex-grow: 1`) with 3 rows; stats bar; bottom control strip
  (problem text + FIRE button row + ▲▼ button row).
- Minimum supported width: 320 px.
- Game area height: `calc(100dvh - 200px)` with `100vh` fallback.

**Font sizes**: same rules as Activities 1 and 2 (`:root 16px`; `400px` and landscape
`500px` breakpoints).

**Tap target sizes**: minimum `48 × 48 px` for ▲, ▼, and FIRE buttons.

**Safe-area insets**: `env(safe-area-inset-*)` applied to `body`.

---

## Phase A3.2 — HTML/JS/CSS Implementation (Activity 3)

### Goal

Implement `main/dyn_activities/shoot_math.html` as a single self-contained HTML5 file
following all rules from Phase A3.1 and the esport-fi32 dynamic activity interface contract.

### Inputs

- Phase A3.1 (this document).
- `main/dyn_activities/space_math.html` — reference for credit-claim flow, URL param
  parsing, `formatHMS()`, `showBanner()`, star-field background CSS, and explosion
  animation keyframes.
- `docs/1-specification.md` §6.9 (credit API), §6.10 (dynamic activities URL contract).

### File

`main/dyn_activities/shoot_math.html`

### Required URL Query Parameters

| Parameter      | Type   | Description                                              |
| -------------- | ------ | -------------------------------------------------------- |
| `pin`          | string | 8-char device PIN from `GET /api/dyn`                    |
| `device_idx`   | int    | Device slot index (0–3)                                  |
| `act_id`       | int    | Activity ID                                              |
| `credits_s`    | int    | Reference credit in seconds (`activity.credit_s`)        |
| `time_limit_s` | int    | Activity time limit in seconds (`activity.time_limit_s`) |
| `token`        | string | One-time nonce from `GET /api/dyn`                       |

If any required parameter is absent, zero (for numeric params), or fails to parse, the
game must show the `ERROR` screen immediately before any game logic runs.

### Structural Requirements

1. **Single file, no external resources.**  All CSS, JS, and graphics are inline.
2. **File size target < 40 KB** (uncompressed, unminified source).  Hard maximum: 80 KB.
   See [Flash Storage Constraints](#flash-storage-constraints).
3. **`<meta charset="UTF-8">` and `<meta name="viewport" ...>`** present (exact form
   specified in Phase A3.1).
4. **`<title>Shoot Math Asteroids</title>`**.
5. **CSS reset**: `box-sizing: border-box` on `*`; `margin: 0; padding: 0` on `body`.
6. **Dark space theme**: `background: #0a0a2e` on body.
7. **No `alert()`, `confirm()`, `prompt()`, `eval()`, or `innerHTML` injection of
   user-supplied data.**  Only game-generated integers and operator symbols are inserted
   via `textContent`.
8. **`var` is forbidden** — use `const` and `let` throughout.
9. **Game loop**: `requestAnimationFrame` for set travel progress tracking and laser
   animation; a separate `setInterval(1000)` decrements the countdown timer and checks
   win/lose conditions.

### JavaScript Architecture

```
┌─ Constants (SECS_PER_PROBLEM, EARN_FRACTION, ROWS, SPEED_TIERS)
├─ State variables
│   ├─ pin, deviceIdx, actId, creditsS, timeLimitS, token  (URL params)
│   ├─ selectedTypes[]  ('mul' | 'add' | 'sub')
│   ├─ flyDir            ('rtl' | 'ltr')
│   ├─ targetProblems, solved, wrong, elapsedS
│   ├─ gameState: 'loading' | 'setup' | 'playing' | 'end'
│   ├─ rocketRow          (0 | 1 | 2)
│   └─ currentSet         { problem, correctRow, answers[3], spawnTime, travelDuration }
├─ Problem & distractor generator
│   ├─ generateProblem() → { text, answer }
│   └─ generateDistractors(correct, type) → [d1, d2]
├─ Set lifecycle
│   ├─ spawnSet()           → generates problem + assigns answers to rows; starts CSS animation
│   ├─ checkCollision()     ← called from RAF; fires when elapsed >= travelDuration
│   ├─ resolveSet(row)      → evaluates answer; updates solved/wrong/timer
│   └─ fleeAsteroids(survivedRow)  ← triggers flee CSS transitions; survivedRow = -1 flees all
├─ Input handling
│   ├─ onMoveUp()
│   ├─ onMoveDown()
│   ├─ onFire()             ← fires at rocketRow; resolves set (correct or wrong)
│   ├─ onTapAsteroid(row)   ← snap rocket to row + immediate fire
│   └─ onKeyDown(event)     ← ArrowUp, ArrowDown, Space
├─ Timer
│   └─ onTick()             ← setInterval(1000)
├─ Screen renderers
│   ├─ renderSetup()        ← checkboxes + direction radio + Start Game! button
│   ├─ renderPlaying()      ← builds game DOM once; updates via updateRocketRow() and spawnSet()
│   ├─ updateRocketRow()    ← moves rocket element; updates row highlights
│   ├─ renderEnd(win)
│   └─ renderError(msg)
└─ Credit claim
    └─ claimCredits()       ← same flow as space_math.html
```

### CSS Architecture

```
:root            → colour vars (--bg, --accent, --win-gold, --laser-color, --rock-grad, ...)
body             → dark bg; landscape: flex row; portrait: flex column
                   safe-area-inset padding applied
body.ltr         → flex-direction: row-reverse  (LTR: control panel on left)
.game-col        → flex: 1 1 0; header + game-area
.control-col     → 200 px fixed (landscape); full-width bottom strip (portrait)
.header-bar      → flex row; title, timer, progress bar
.game-area       → position: relative; overflow: hidden; flex-grow: 1
.star-layer      → position: absolute; full size; animated background (reuse space_math pattern)
.row             → height: 33.33%; position: relative; border-bottom dotted divider
.row.active      → background-tint rgba(255,233,74,0.08)
.asteroid        → position: absolute; CSS oval; animation: flyIn linear
.asteroid.tried  → opacity: 0.5; border: 2px solid #ff4444
.asteroid.explode → keyframe radial burst (reuse from space_math pattern)
.rocket          → position: absolute; right: 8px (RTL) / left: 8px (LTR); inline SVG
                   transition: top 0ms (instant snap)
.rocket.correct  → @keyframes green brightness flash (200 ms)
.rocket.wrong    → @keyframes horizontal shake (300 ms)
.rocket-pulse    → @keyframes scale(1.2) pulse on row change (80 ms)
.laser           → position: absolute; height: 3px; @keyframes scaleX expand (150 ms)
.laser.wrong     → background switches to #ff4444
.problem-display → large bold; centred in .control-col (landscape) or bottom strip (portrait)
.move-btn        → min 48×48 px; ▲ / ▼ arrows
.fire-btn        → min 48×48 px; prominent --accent background
.setup-screen    → same pattern as space_math.html
.direction-group → flex column; radio-label rows; accent-color for radio inputs
.end-screen      → centred; star-burst background on WIN
```

### Acceptance Criteria

- [ ] File is a valid HTML5 document with no external resource references.
- [ ] File size ≤ 40 KB uncompressed (target); must not exceed 80 KB under any circumstances.
- [ ] Source code is human-readable; no minification, no obfuscation.
- [ ] Missing/zero URL params → ERROR screen shown; game does not start.
- [ ] Valid URL params → SETUP screen shown before any game logic runs.
- [ ] SETUP screen: three problem-type checkboxes (all checked by default) + direction
      radio group (RTL default).
- [ ] **Start Game!** button disabled when no checkbox checked; enabled on first checkbox
      check.  Direction radio does not affect enabled state.
- [ ] `flyDir = 'rtl'`: rocket on right, asteroids from left; rocket SVG faces left.
- [ ] `flyDir = 'ltr'`: rocket on left, asteroids from right; rocket SVG faces right;
      `.control-col` moves to left side; asteroid animation direction reversed.
- [ ] `targetProblems = floor(time_limit_s * 0.70 / 5)`, minimum 3.
- [ ] Each set: 1 correct answer + 2 plausible distractors; randomly assigned to rows
      0, 1, 2.
- [ ] Math problem is displayed in the control panel (outside the game area) for the
      full duration of each set.
- [ ] Only problem types in `selectedTypes[]` are generated.
- [ ] ▲ button / ArrowUp key → rocket moves to row above; wraps row 0 → row 2.
- [ ] ▼ button / ArrowDown key → rocket moves to row below; wraps row 2 → row 0.
- [ ] Row change is instant (discrete snap); brief scale-pulse on rocket (80 ms).
- [ ] Active row visually highlighted (background tint + rocket glow).
- [ ] FIRE button / Space key → laser shoots at active row's asteroid.
- [ ] Tapping an asteroid → rocket snaps to that row + immediate shot.
- [ ] Laser animation plays (150 ms expanding line from rocket to asteroid).
- [ ] Correct asteroid shot → orange explosion; remaining 2 flee off-screen;
      rocket green flash; `solved++`.
- [ ] Wrong asteroid shot → red laser flash; `wrong++`; −1 s from timer;
      all asteroids in the set immediately flee off-screen; new set spawns.
- [ ] Wrong collision → rocket red shake; `wrong++`; −1 s; all asteroids flee; `solved` unchanged.
- [ ] New set spawns immediately after previous set resolves.
- [ ] Asteroid travel time follows the 4-tier speed table (8 s → 7 s → 6 s → 5 s).
- [ ] Countdown timer decrements every second; displayed as `MM:SS` in header.
- [ ] Progress bar reflects `solved / targetProblems` (capped at 100%).
- [ ] `solved >= targetProblems` before timer → WIN screen; bonus count displayed.
- [ ] Timer reaches 0 → GAME OVER screen; partial credit displayed.
- [ ] WIN screen: "BULLSEYE! YOU WIN!" + solved/target, bonus (if any), credits as h:mm:ss.
- [ ] GAME OVER screen: "TIME'S UP!" + solved/target, credits as h:mm:ss.
- [ ] Credit claim POST sent automatically on END screen; same fields as Activities 1 and 2.
- [ ] HTTP 200 → green banner; 403 → red (non-retriable); 429 → orange (non-retriable);
      other → red + Try again button.
- [ ] Star-field background animates continuously.
- [ ] No `var`, no `eval()`, no `alert()/confirm()`, no external resources.
- [ ] `<meta name="viewport">` includes `maximum-scale=1.0` and `user-scalable=no`.
- [ ] Landscape: `.control-col` is 200 px; game area fills remaining width.
- [ ] Portrait: controls (problem + FIRE + ▲▼) appear in bottom strip below game area.
- [ ] No horizontal scrollbar at 320 px wide (portrait) or 568 × 320 px (landscape phone).
- [ ] `env(safe-area-inset-*)` applied to `body`.
- [ ] Game area uses `100dvh` with `100vh` fallback for height calculations.

### Required `POST /api/activities/credit` Fields

```javascript
{
  device_idx:        deviceIdx,
  act_id:            actId,
  credits_s:         submitCredit,  // Math.floor(creditsS * solved / targetProblems), capped at creditsS
  completion_time_s: elapsedS,
  pin:               pin,
  token:             token
}
```

---

## Phase A3.3 — Integration & Verification (Activity 3)

### Goal

Register `shoot_math.html` as a dynamic activity in the firmware, wire it to a test
device, and verify the complete play-through flow end-to-end.

### Inputs

- Phase A3.2 output: `main/dyn_activities/shoot_math.html`.
- Existing Feature 8 infrastructure (Phases 8.1–8.5 must be complete).

### Tasks

1. **Build**: run `idf.py reconfigure && idf.py build`.  The CMake glob picks up
   `shoot_math.html` automatically; `g_dyn_act_count` increments by 1.

2. **Create activity via admin UI**:
   - Navigate to `/activities/manage`.
   - Tick "Dynamic activity".
   - Select `shoot_math` from the combobox.
   - Set **Credit** to `0:10:00` (600 s), **Time Limit** to `0:02:00` (120 s),
     **Daily Limit** to `2`.
   - Submit → HTTP 303 redirect.

3. **Assign to device 0** via the same manage page.

4. **Kid workflow test (RTL direction)**:
   - Navigate to `/dyn` from device 0; verify `shoot_math` button appears.
   - Click button; confirm URL contains all 6 required parameters including
     `time_limit_s=120`.
   - SETUP screen: verify three checkboxes (all checked), direction radio (RTL default),
     **Start Game!** disabled if all unchecked.
   - Play to WIN (solve ≥ 16 problems before 2 min expire); claim credits; verify green
     success banner and `new_counter_hms`.

5. **LTR direction test**: repeat step 4 selecting "Asteroids fly left → right" on SETUP;
   verify rocket appears on the **left** and asteroids travel from the **right**; controls
   panel is on the left; gameplay and scoring unchanged.

6. **Wrong-answer penalty test**: fire at a wrong asteroid; verify −1 s deducted from
   timer, `wrong` increments, asteroid dims with red border.

7. **Re-fire ignored test**: fire at the same wrong asteroid a second time; verify timer
   is NOT further decremented and `wrong` is NOT incremented again.

8. **Collision test (correct row)**: let a set reach the rocket while positioned on the
   correct-answer row; verify `solved` increments and others flee.

9. **Collision test (wrong row)**: let a set reach the rocket while positioned on a
   wrong-answer row; verify `wrong` increments, −1 s, set discards, `solved` unchanged.

10. **GAME OVER path**: let timer expire at < 16 solved; verify proportional credit
    screen.

11. **Replay attack test**: after claiming, re-submit same POST (same `token`) →
    server must return HTTP 403.

12. **Daily limit test**: claim twice (daily limit = 2); third attempt → HTTP 429
    banner shown in game.

13. **Tampered `time_limit_s` test**: load URL with `time_limit_s=5`; verify
    `targetProblems` clamps to 3; game is playable; credit cap still applies.

14. **Responsiveness test**: load game on a 320 px portrait viewport (browser DevTools);
    all controls (▲, ▼, FIRE, problem text) must be visible without horizontal scroll.

15. **Build artefact check**: `g_dyn_act_registry` must include an entry with
    `p_name == "shoot_math"`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings.
- [ ] `g_dyn_act_count` increases by 1 compared to pre-game build.
- [ ] `GET /dyn_activities/shoot_math` returns HTTP 200 with `Content-Type: text/html`.
- [ ] `GET /dyn_activities/shoot_math.html` returns HTTP 200 (`.html` extension handled).
- [ ] `/dyn` page shows `shoot_math` button for registered device.
- [ ] Launch URL contains all 6 required parameters including `time_limit_s`.
- [ ] RTL mode: rocket on right, asteroids from left; controls on right.
- [ ] LTR mode: rocket on left, asteroids from right; controls on left; layout mirrored.
- [ ] WIN flow: `POST /api/activities/credit` returns 200; counter incremented correctly.
- [ ] GAME OVER flow: proportional credits posted and accepted.
- [ ] Wrong shot: −1 s applied; `wrong` incremented; asteroid dims with red border.
- [ ] Re-fire on tried-wrong asteroid: no second penalty, no `wrong` increment.
- [ ] Correct collision (rocket on correct row): `solved` increments; others flee.
- [ ] Wrong collision (rocket on wrong row): `wrong` increments; −1 s; set discards.
- [ ] Replay of consumed token → HTTP 403.
- [ ] Third daily claim → HTTP 429.
- [ ] `time_limit_s=5` → `targetProblems == 3` (minimum clamp enforced).
- [ ] No regression in Activities 1 and 2 tests or Phases 8.1–8.5 criteria.

---

---

## Activity 4 — Math Battleship: Grid Commander

### Concept

The player is a naval commander at a sonar console.  Enemy ships are hidden in a
**10 × 10 multiplication table grid**.  Rows are numbered **V = 1–10** (top to bottom)
and columns are numbered **H = 1–10** (left to right), so each cell (V, H) directly
represents the multiplication fact V × H.

To fire a torpedo at a target cell, the player fills in three input boxes:

```
V [__]  ×  H [__]  =  [____]
```

- **V** selects the row (1–10); **H** selects the column (1–10); the rightmost box
  must contain the product V × H.
- **Correct answer:** the torpedo fires at cell (V, H).  If a ship occupies that cell,
  the segment is revealed as a **hit** 🔥.  If not, the cell is marked as a **miss** 〜.
  No shot is consumed for a miss — only correct answers trigger a shot.
- **Wrong answer:** one shot is lost **without** firing.  The cell is not revealed.
- The player starts with **10 shots**.  A **streak mechanic** awards +1 bonus shot every
  3 consecutive correct answers, resetting the streak counter on each bonus.  A wrong
  answer resets the streak to zero.
- An already-shot cell (hit or miss) cannot be targeted again; the FIRE button is
  disabled when (V, H) corresponds to a cell that has already been revealed.

**Tap-to-target:** tapping any unrevealed grid cell pre-fills the V and H input boxes
and moves focus to the answer box.  The player only needs to type the product.  Tapping
an already-revealed cell has no effect.

**Win condition:** all enemy ships fully sunk (all 19 ship cells hit) before shots are
exhausted and before time expires.

**Engagement hooks:**
- Sonar/radar aesthetic: phosphor-green grid lines on dark navy, subtle rotating
  conic-gradient radar sweep overlay (pure CSS animation).
- Per-cell hit and miss animations (CSS keyframe bursts and ripples).
- "SUNK! 🔥" toast notification when a ship is fully sunk, with the ship's cells
  flashing simultaneously.
- Shot counter displayed as torpedo icons; streak counter with ⚡ glow; bonus shot
  animation on streak completion.
- Ship status strip showing each ship's health segments — intact segments bright,
  hit segments dark.
- Full grid revealed on game end (all ship positions shown).
- Sound is NOT used (avoids permission issues and distraction in shared spaces).

Size budget: target < 200 KB, hard maximum 400 KB uncompressed; **< 100 KB compressed**
(the figure that matters for flash allocation).
See [Flash Storage Constraints](#flash-storage-constraints).

---

### Math Problem Rules

This game uses **multiplication only**.  The 10 × 10 grid maps directly to the standard
multiplication table, so restricting to one operation is both educationally coherent and
mechanically self-consistent.

| Type           | Operand ranges                           | Result constraint | Example      |
| -------------- | ---------------------------------------- | ----------------- | ------------ |
| Multiplication | V: 1–10 (player-chosen row); H: 1–10 (player-chosen column) | Product 1–100 | `7 × 8 = ?` |

- **Problems are player-chosen, not randomly generated.**  The educational challenge is
  strategic: the player selects which cell to target and must prove knowledge of that
  specific fact.
- A child who knows their tables can aim precisely; one who does not will waste shots on
  wrong answers.  Mathematical fluency is directly rewarded with more efficient play.

---

### Timing & Scoring Model

A single global countdown timer runs for `time_limit_s` seconds.  There is no per-shot
countdown — the player can think between shots.

**Constants (hardcoded in the HTML):**

| Constant              | Value | Rationale                                                          |
| --------------------- | ----- | ------------------------------------------------------------------ |
| `STARTING_SHOTS`      | 10    | Enough shots for a strategic game; not enough for brute-force (100 cells) |
| `STREAK_BONUS_AT`     | 3     | Bonus shot every 3 consecutive correct answers; rewards mathematical fluency |
| `TOTAL_SHIP_CELLS`    | 19    | Carrier(5) + Battleship(4) + 2×Destroyer(3) + 2×PatrolBoat(2)     |

**Fleet composition:**

| Ship name    | Length | Count | Total cells |
| ------------ | ------ | ----- | ----------- |
| Carrier      | 5      | 1     | 5           |
| Battleship   | 4      | 1     | 4           |
| Destroyer    | 3      | 2     | 6           |
| Patrol Boat  | 2      | 2     | 4           |
| **Total**    |        | **6** | **19**      |

There is no `targetProblems` concept as in Activities 1–3.  Credit is proportional to
the number of ship cells hit.

---

### Credit Calculation

```
cellsHit     = number of distinct ship cells revealed as hits
earnedCredit = Math.floor(credit_s * cellsHit / TOTAL_SHIP_CELLS)
submitCredit = Math.min(earnedCredit, credit_s)   // server will cap anyway
```

- `cellsHit == 0` → `earnedCredit = 0` → `submitCredit = 0`.  Claim POST still sent.
- `cellsHit == 19` (all ships sunk) → WIN state; `earnedCredit == credit_s` (full credit).
- Shots exhausted before all ships sunk → GAME OVER; proportional credit based on
  `cellsHit` at the moment shots reach zero.
- Time expires before all ships sunk → GAME OVER; proportional credit based on `cellsHit`
  at the moment the timer reaches zero.

---

### URL Parameters & API Integration

Identical to Activities 1–3 — same six URL parameters (`pin`, `device_idx`, `act_id`,
`credits_s`, `time_limit_s`, `token`), same `POST /api/activities/credit` contract, and
same response handling.

`time_limit_s` is used directly as the global countdown (no `targetProblems` derivation).
`credits_s` is the maximum credit awarded for sinking the entire fleet.

Launch URL:

```
/dyn_activities/battle_math?pin=<PIN>&device_idx=<N>&act_id=<ID>&credits_s=<CREDIT_S>&time_limit_s=<N>&token=<TOKEN>
```

Credit claim POST: identical to Activities 1–3 (`POST /api/activities/credit` with the
same six fields).

---

### Security Considerations

Identical to Activities 1–3.  `time_limit_s` is informational — a tampered-up value
gives more countdown time but does not increase the credit cap.  Ship placement is
generated client-side; the server performs no placement validation and trusts only
`credits_s` (capped at `activity.credit_s`) and `completion_time_s` (informational).
There is no incentive to forge a higher `credits_s` in the URL.

---

## Phase A4.1 — Game Design & UI Specification (Activity 4)

### Goal

Define the full visual layout, grid mechanics, cell state model, ship placement
algorithm, input behaviour, animation specification, and all screen states for
`battle_math.html` so that Phase A4.2 can proceed without ambiguity.

---

### Screen States

| State     | Trigger                                        | Description                                               |
| --------- | ---------------------------------------------- | --------------------------------------------------------- |
| `LOADING` | Page load                                      | Validates URL params; transitions to `SETUP` or `ERROR`   |
| `ERROR`   | Missing/invalid URL params                     | Error message + `← Back to Games` link                   |
| `SETUP`   | After `LOADING` succeeds                       | Fleet briefing + instructions → **Launch Torpedoes!**     |
| `PLAYING` | Player presses **Launch Torpedoes!**           | Main turn-based game loop                                 |
| `END`     | All ships sunk (WIN) OR shots = 0 OR timer = 0 | WIN or GAME OVER result; credit claim auto-triggered      |

Same single `<div id="screen">` swap pattern as Activities 1–3.

---

### SETUP State Layout

```
┌──────────────────────────────────────┐
│   ⚓  Grid Commander                  │
│   Naval Multiplication               │
│                                      │
│  FLEET BRIEFING:                     │
│  🚢 Carrier      ■■■■■  (5 cells)    │
│  ⛴  Battleship   ■■■■   (4 cells)    │
│  🚤 Destroyer ×2  ■■■    (3 cells)   │
│  ⛵ Patrol ×2     ■■     (2 cells)   │
│                                      │
│  HOW TO PLAY:                        │
│  Pick row V and column H, then type  │
│  V × H to fire a torpedo.  Correct = │
│  shoot; wrong = lose a shot.  Sink   │
│  all ships to win!                   │
│                                      │
│  [ Launch Torpedoes! ]               │
└──────────────────────────────────────┘
```

- No problem-type checkboxes (multiplication is the only operation).
- Ship segments in the briefing use small `<span>` blocks styled as coloured squares
  (`8 × 8 px`, `background: #4af`, `border-radius: 2px`) — same visual language as the
  in-game ship segments.
- The **Launch Torpedoes!** button is always enabled (no configuration required).
- The setup screen uses the same dark-navy ocean theme (`#001220` background) as the game.

---

### PLAYING State Layout

Both portrait and landscape orientations use the **same single-column layout** — no
right-column control panel.  The grid occupies all available vertical space between the
header bar and the input strip, maximising the battlefield in both orientations.

```
┌──────────────────────────────────────────────┐
│ ⚓  💣 ×8   ⚡×2  ⏱ 1:45  🚢■■■■■ ⛴■■■■...│  ← header bar (single row)
├────┬─────────────────────────────────────────┤
│    │  1    2    3    4    5    6    7  … 10   │  ← column (H) labels
│  1 │[   ][   ][   ][   ][   ][   ][   ]…[   ]│
│  2 │[   ][   ][   ][ 🔥][   ][   ][   ]…[   ]│  ← hit cell (V=2, H=4)
│  3 │[   ][   ][   ][   ][ 〜][   ][   ]…[   ]│  ← miss cell (V=3, H=5)
│    │  …                                      │  ← grid fills flex: 1 1 0
│ 10 │[   ][   ][   ][   ][   ][   ][   ]…[   ]│
├────┴─────────────────────────────────────────┤
│  V [__]  ×  H [__]  =  [______]  [🔥 FIRE!] │  ← input strip (fixed height)
│  ⚠ Already targeted! Choose another cell.   │  ← warning label (hidden when n/a)
└──────────────────────────────────────────────┘
```

**Header bar fields (left to right):**
- `⚓` title glyph (small, does not spell out the full title to save space).
- `💣 ×N` — shots remaining; the number pulses briefly (CSS scale animation) when
  a shot is gained or lost.
- `⚡×N` — current consecutive-correct streak (0–2); shows `⚡×0` when no streak;
  when `N` hits 3, a bonus shot fires and the counter resets.
- `⏱ MM:SS` — countdown timer.
- Ship status segments (right-aligned): one group per ship type showing small coloured
  squares; intact segments are bright (`#4af`), hit segments are dark (`#222`), sunk
  ships are crossed (`#f44` with a diagonal SVG line or opacity 0.3).  Font size
  `0.65em` keeps this compact.

**In landscape orientation** the header remains a single row.  The grid cells grow taller
(more vertical space available) but the single-column layout is unchanged — there is no
right-side control panel.  The grid auto-scales uniformly in both dimensions because cell
width and height are kept equal via `aspect-ratio: 1`.

---

### Grid CSS Layout

The grid is implemented with CSS Grid:

```css
.grid {
    display: grid;
    grid-template-columns: 1.8em repeat(10, 1fr);  /* row-label col + 10 data cols */
    grid-template-rows:    1.4em repeat(10, 1fr);  /* col-label row + 10 data rows */
}
.cell {
    aspect-ratio: 1;  /* cells are always square regardless of viewport */
    min-width: 0;
    min-height: 0;
}
```

The `.grid-wrap` container is `flex: 1 1 0; min-height: 0; overflow: hidden`, so the
grid expands to fill all space between the header and the input strip.  With `1fr`
columns and `aspect-ratio: 1` on each cell, the grid auto-scales uniformly in both
portrait and landscape without any JS size calculations.

**Minimum cell size:** 28 px (enforced via `min-width: 28px` on `.grid`'s data columns)
so cells remain tappable on a 320 px phone (320 − 1.8 em label ≈ 293 px / 10 ≈ 29 px
per cell).

---

### Cell State Model

Each cell is in exactly one of these states at any time:

| State        | CSS class        | Visual description                                                          |
| ------------ | ---------------- | --------------------------------------------------------------------------- |
| `unknown`    | `.cell`          | Dark navy `#001a2e`; subtle green border `#003828`; hover tint `#003040`.  Tappable — pre-fills V and H. |
| `targeted`   | `.cell.targeted` | Pulsing bright-green ring (`@keyframes targetPulse`); pre-filled in inputs. Tappable (re-selects). |
| `hit`        | `.cell.hit`      | Orange-red background `#c04010`; 🔥 emoji centred; `@keyframes hitFlash` on entry. Not tappable. |
| `miss`       | `.cell.miss`     | Dark blue `#002040`; `〜` text centred; `@keyframes missRipple` on entry.  Not tappable. |
| `sunk`       | `.cell.sunk`     | Bright red `#e83020`; 💥 emoji; `@keyframes sunkPulse` (3× repeating) on entry. Cells that were `.hit` transition to `.sunk` when the ship is fully hit. |

**Targeted cell highlight is live:** as the player types into the V and H inputs (both
valid 1–10 numbers), the corresponding cell gains the `.targeted` class immediately.
When the player clears V or H, the `.targeted` class is removed.

---

### Input Strip Behaviour

The strip is a horizontal flex row pinned to the bottom of the screen:

```
  V [input]  ×  H [input]  =  [input]  [ 🔥 FIRE! ]
```

| Element        | Type / HTML                                        | Constraints                          |
| -------------- | -------------------------------------------------- | ------------------------------------ |
| `V` label      | `<label>`                                          | Tap targets the V input              |
| V input        | `<input type="number" min="1" max="10" step="1">`  | Required; range 1–10                 |
| `×` separator  | `<span>`                                           | Non-interactive                      |
| `H` label      | `<label>`                                          | Tap targets the H input              |
| H input        | `<input type="number" min="1" max="10" step="1">`  | Required; range 1–10                 |
| `=` separator  | `<span>`                                           | Non-interactive                      |
| Answer input   | `<input type="text" inputmode="numeric" pattern="[0-9]*" maxlength="3">` | Required; 1–100 |
| FIRE! button   | `<button>`                                         | See disabled conditions below        |

**Tab / Enter navigation:** Tab advances focus V → H → answer.  Enter on the answer
field triggers FIRE (same as clicking the button).

**FIRE button disabled conditions (any of the following):**
1. V input is empty, invalid, or outside 1–10.
2. H input is empty, invalid, or outside 1–10.
3. Answer input is empty.
4. Cell (V, H) has already been revealed (hit or miss) — show the already-targeted warning.

Note: the FIRE button is **not** disabled when the answer is wrong.  Wrong-answer
detection happens on submit, not on keystroke, so the player does not receive
correctness feedback before pressing FIRE.

**Already-targeted warning:** a one-line `<p class="warn-msg">` below the input strip
that reads `⚠ Already targeted! Choose another cell.`  It is `visibility: hidden` by
default and becomes `visibility: visible` only when conditions 1–3 are met but condition
4 prevents firing (i.e. all inputs are valid but the cell is already shot).

**Tap-to-target interaction:** tapping a `.cell` (not `.cell.hit`, `.cell.miss`, or
`.cell.sunk`) sets the V and H inputs to the cell's row and column, removes
`.targeted` from any previously highlighted cell, adds `.targeted` to the tapped cell,
clears the answer input, and moves focus to the answer input.

---

### Shot Mechanics

| Event                                         | Effect                                                                      |
| --------------------------------------------- | --------------------------------------------------------------------------- |
| FIRE with correct answer, hit cell            | `shotsRemaining` unchanged; `cellsHit++`; `streakCount++`; `hitCell()`.    |
| FIRE with correct answer, miss cell           | `shotsRemaining` unchanged; `streakCount++`; `missCell()`.                 |
| FIRE with wrong answer                        | `shotsRemaining--`; `streakCount = 0`; no cell revealed; shake animation.  |
| `streakCount` reaches `STREAK_BONUS_AT` (3)   | `shotsRemaining++`; `streakCount = 0`; bonus-shot pulse animation.         |
| `shotsRemaining` reaches 0                    | `endGame('no_shots')`.                                                      |
| Timer reaches 0                               | `endGame('timeout')`.                                                       |
| All 19 ship cells hit                         | `endGame('win')`.                                                           |

---

### Ship Placement Algorithm

Ships are placed randomly client-side using `Math.random()`.  The algorithm must
guarantee the following rules:

1. All cells of a ship are within the 10 × 10 grid (1–10 for both V and H).
2. Ships may be horizontal (constant V, varying H) or vertical (varying V, constant H).
   Orientation is chosen at random (50/50).
3. No two ships overlap (their cell sets are disjoint).
4. No two ships are adjacent (a 1-cell buffer in all 8 directions is enforced between
   any two ships).  This prevents ambiguous partial-sink scenarios where hit cells from
   two ships are side-by-side.
5. Placement is attempted by shuffling a random order for each ship and retrying up to
   200 times if a placement violates rules 1–4.  A placement failure after 200 attempts
   retriggers the entire placement from scratch (rare; the 10 × 10 grid accommodates
   19 cells with buffer comfortably).

The final `ships[]` array is never serialised to the URL or sent to the server — it
exists only in JS memory during the game session.

---

### Animation Specifications

**`@keyframes hitFlash`** (triggered on a `.hit` cell):
- 0 %: `background: #001a2e; transform: scale(1)`
- 30 %: `background: #ff8020; transform: scale(1.15)` (orange burst)
- 100 %: `background: #c04010; transform: scale(1)` (settles to hit colour)
- Duration: 500 ms, fill: forwards.

**`@keyframes missRipple`** (triggered on a `.miss` cell):
- 0 %: `box-shadow: 0 0 0 0 rgba(60,120,255,0.6)`
- 60 %: `box-shadow: 0 0 0 8px rgba(60,120,255,0)`
- 100 %: `box-shadow: 0 0 0 0 rgba(60,120,255,0)`
- Duration: 400 ms.

**`@keyframes sunkPulse`** (triggered on all cells of a fully sunk ship):
- 0 %, 100 %: `background: #e83020`
- 50 %: `background: #ff6040; box-shadow: 0 0 12px 4px #ff4020`
- Duration: 400 ms; iteration-count: 3.

**`@keyframes targetPulse`** (continuous on `.targeted` cell):
- 0 %, 100 %: `box-shadow: 0 0 0 2px #00ff88`
- 50 %: `box-shadow: 0 0 0 5px #00ff88, 0 0 14px 4px rgba(0,255,136,0.4)`
- Duration: 1.2 s; infinite.

**`@keyframes shake`** (wrong answer — applied to input strip `.input-strip`):
- `0%, 100%: translateX(0); 20%, 60%: translateX(-6px); 40%, 80%: translateX(6px)`
- Duration: 380 ms.  Strip border briefly turns `#ff4444`.

**`@keyframes radarSweep`** (continuous CSS radar overlay):
- A `<div class="radar-overlay">` is positioned `absolute` over `.grid-wrap`.
- `background: conic-gradient(from 0deg, rgba(0,255,136,0.10) 0deg, transparent 25deg, transparent 360deg)`
- `@keyframes radarSweep { from { transform: rotate(0deg) } to { transform: rotate(360deg) } }`
- `animation: radarSweep 4s linear infinite`
- `pointer-events: none` (does not intercept taps).
- `border-radius: 0` (square grid, not circular — the gradient sweeps across the grid).

**`@keyframes bonusPulse`** (bonus shot awarded — applied to `.shot-counter`):
- Brief `transform: scale(1.35)` + `color: #00ff88` flash, 350 ms.

**"SUNK!" toast:**
- `<div class="toast">🔥 [Ship name] SUNK!</div>` inserted into `<body>`.
- `position: fixed; top: 10px; left: 50%; transform: translateX(-50%)`.
- `@keyframes toastSlide`: `opacity 0 → 1` over 200 ms, holds for 1400 ms, `opacity 1 → 0` over 400 ms.
- Removed from DOM after 2000 ms total.

---

### Background

- Body background: `#001220` (very dark navy).
- `.grid-wrap` background: `#001a2e`.
- Grid cell borders: `1px solid #003828` (dark phosphor-green).
- Header bar: `rgba(0, 10, 20, 0.9)` with a subtle bottom border `1px solid #004030`.
- Input strip: `rgba(0, 8, 18, 0.95)` with a subtle top border `1px solid #003028`.
- No external images; all colour is CSS.

---

### WIN State Layout

```
┌──────────────────────────────────────┐
│   ⚓  FLEET DESTROYED! 🎉             │
│                                      │
│  All 6 ships sunk!  💥               │
│  You fired N shots.                  │
│  Time remaining: M:SS               │
│                                      │
│  Credits earned: h:mm:ss             │
│                                      │
│  Saving your credits…                │
│  ← Back to Games                     │
└──────────────────────────────────────┘
```

WIN title colour: `#00ff88` (phosphor-green) with a matching `text-shadow` glow.
The full grid is revealed (all ships shown) after a 600 ms celebration delay.

---

### GAME OVER State Layout (shots exhausted)

```
┌──────────────────────────────────────┐
│   💣  OUT OF AMMO!                   │
│                                      │
│  You sank N / 6 ships.               │
│  Cells hit: N / 19                   │
│  Credits earned: h:mm:ss             │
│                                      │
│  Saving your credits…                │
│  ← Back to Games                     │
└──────────────────────────────────────┘
```

---

### GAME OVER State Layout (time expired)

```
┌──────────────────────────────────────┐
│   ⏱  TIME'S UP!                      │
│                                      │
│  You sank N / 6 ships.               │
│  Cells hit: N / 19                   │
│  Credits earned: h:mm:ss             │
│                                      │
│  Saving your credits…                │
│  ← Back to Games                     │
└──────────────────────────────────────┘
```

On both GAME OVER screens, the full grid is revealed (all ship positions shown) 600 ms
after the END screen renders.

---

### Responsiveness and Orientation

**Primary target**: tablet (768–1024 px) in either orientation; smartphone (360–414 px)
in portrait.  The single-column layout with bottom input strip works equally in both
orientations — no layout switch is required.

**Viewport meta tag** (required — must be exactly this):

```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
```

**Portrait and landscape:** identical structure (`flex-direction: column` always).
- Header at top (`flex-shrink: 0`).
- `.grid-wrap` (`flex: 1 1 0; min-height: 0; overflow: hidden`): fills all remaining
  space; the CSS Grid inside auto-scales cells via `1fr` + `aspect-ratio: 1`.
- Input strip at bottom (`flex-shrink: 0`).

**Minimum supported width:** 320 px.  At 320 px: label column ≈ 29 px; 10 data columns
= (320 − 29) / 10 ≈ 29 px per cell — at the 28 px minimum, workable with tap-to-target
eliminating the need for precision tapping.

**Input strip on narrow screens:** use `flex-wrap: nowrap` with `font-size: clamp(12px,
3.5vw, 15px)` on the strip so all elements fit on one line down to 320 px.  Input widths
are `3em` for V/H and `4em` for the answer.

**Font sizes:**
- `:root { font-size: 16px }`.
- `@media (max-width: 400px) { :root { font-size: 13px } }`.
- `@media (orientation: landscape) and (max-height: 500px) { :root { font-size: 12px } }`.

**Safe-area insets:** `padding: env(safe-area-inset-top) … env(safe-area-inset-bottom) …`
applied to `body`.

**Keypad / input tap targets:** V and H inputs: minimum `44 × 44 px`.  FIRE button:
minimum `44 px` height; width `min-content` but at least 80 px.

---

## Phase A4.2 — HTML/JS/CSS Implementation (Activity 4)

### Goal

Implement `main/dyn_activities/battle_math.html` as a single self-contained HTML5 file
following all rules from Phase A4.1 and the esport-fi32 dynamic activity interface
contract.

### Inputs

- Phase A4.1 (this document).
- `main/dyn_activities/space_math.html` — reference for credit-claim POST flow, URL param
  parsing, `formatHMS()`, `showBanner()`, screen-swap pattern, and timer skeleton.
- `main/dyn_activities/fish_math.html` — reference for ocean colour scheme and dark-navy
  CSS theme.
- `docs/1-specification.md` §6.9 (credit API), §6.10 (dynamic activities URL contract).

### File

`main/dyn_activities/battle_math.html`

### Required URL Query Parameters

| Parameter      | Type   | Description                                              |
| -------------- | ------ | -------------------------------------------------------- |
| `pin`          | string | 8-char device PIN from `GET /api/dyn`                    |
| `device_idx`   | int    | Device slot index (0–3)                                  |
| `act_id`       | int    | Activity ID                                              |
| `credits_s`    | int    | Reference credit in seconds (`activity.credit_s`)        |
| `time_limit_s` | int    | Activity time limit in seconds (`activity.time_limit_s`) |
| `token`        | string | One-time nonce from `GET /api/dyn`                       |

If any required parameter is absent, zero (for numeric params), or fails to parse, the
game must show the `ERROR` screen immediately before any game logic runs.

### Structural Requirements

1. **Single file, no external resources.**  All CSS, JS, and graphics are inline.
2. **File size target < 200 KB** (uncompressed, unminified).  Hard maximum: 400 KB
   uncompressed; **< 100 KB compressed** (the flash-allocation limit).
3. **`<meta charset="UTF-8">` and `<meta name="viewport" ...>`** present (exact form
   specified in Phase A4.1).
4. **`<title>Battle Math: Grid Commander</title>`**.
5. **CSS reset**: `box-sizing: border-box` on `*`; `margin: 0; padding: 0` on `body`.
6. **Dark navy ocean theme**: `background: #001220` on body.
7. **No `alert()`, `confirm()`, or `prompt()`** calls.
8. **No `eval()` or `innerHTML` injection of user-supplied data.**  Only game-generated
   integers (V, H, product) are inserted via `textContent` or numeric template literals.
9. **`var` is forbidden** — use `const` and `let` throughout (ES6 minimum target).
10. **No game loop RAF required** — the game is turn-based.  A `setInterval(1000)`
    decrements the countdown timer.  All cell updates are event-driven (input changes +
    FIRE button click).

### JavaScript Architecture

```
┌─ Constants
│   ├─ STARTING_SHOTS = 10
│   ├─ STREAK_BONUS_AT = 3
│   ├─ TOTAL_SHIP_CELLS = 19
│   └─ FLEET = [ {name, size, count}, … ]  (Carrier×1, Battleship×1, Destroyer×2, Patrol×2)
├─ State variables
│   ├─ pin, deviceIdx, actId, creditsS, timeLimitS, token  (URL params)
│   ├─ gameState: 'loading' | 'setup' | 'playing' | 'end'
│   ├─ grid[10][10]  (cell state per (v-1, h-1): 'unknown' | 'hit' | 'miss')
│   ├─ ships[]       ({ name, cells:[{v,h}], hitCount, sunk })
│   ├─ shotsRemaining
│   ├─ streakCount
│   ├─ cellsHit
│   ├─ shipsSunk
│   ├─ remainingS   (countdown, starts at timeLimitS)
│   ├─ elapsedS     (increments each tick)
│   └─ timerHandle  (setInterval ref, cleared on game end)
├─ Ship placement
│   └─ placeShips()  → populates ships[]; modifies no DOM; pure computation
│       (implements the 4-rule algorithm from Phase A4.1 §Ship Placement Algorithm)
├─ Grid DOM helpers
│   ├─ buildGrid()             → creates label + data cells; attaches tap listeners
│   ├─ cellEl(v, h)            → returns the <div> for cell (v, h)
│   ├─ setCellState(v, h, st)  → updates grid[v-1][h-1] and cell CSS class
│   ├─ setTargeted(v, h)       → adds .targeted; clears previous targeted cell
│   └─ clearTargeted()         → removes .targeted from any highlighted cell
├─ Input handling
│   ├─ onVInput()              → validate; update targeted cell; update FIRE state
│   ├─ onHInput()              → validate; update targeted cell; update FIRE state
│   ├─ onAnswerInput()         → update FIRE state
│   ├─ onAnswerKeydown(e)      → Enter → onFire()
│   ├─ onVHKeydown(e)          → Enter on V → focus H; Enter on H → focus answer
│   ├─ onCellTap(v, h)         → setTargeted; fill inputs; focus answer
│   └─ updateFireBtn()         → enables/disables FIRE; shows/hides warn-msg
├─ Fire logic (onFire)
│   1. Read v, h, ans from inputs (parseInt)
│   2. If grid[v-1][h-1] !== 'unknown': show warn-msg; return
│   3. If ans !== v * h: loseShot(); shakeStrip(); clear answer; return
│   4. streakCount++; if streakCount === STREAK_BONUS_AT: gainBonusShot()
│   5. Find ship at (v, h) (if any)
│   6. If ship found: hitCell(v, h, ship); else: missCell(v, h)
│   7. Clear inputs; clearTargeted(); focus V input; updateFireBtn()
├─ Game state transitions
│   ├─ hitCell(v, h, ship)
│   │     setCellState(v,h,'hit'); cellsHit++; ship.hitCount++
│   │     if ship.hitCount === ship.cells.length: sinkShip(ship)
│   │     else: checkWin()
│   ├─ missCell(v, h)
│   │     setCellState(v,h,'miss')
│   ├─ loseShot()
│   │     shotsRemaining--; updateHeader(); if shotsRemaining===0: endGame('no_shots')
│   ├─ gainBonusShot()
│   │     shotsRemaining++; updateHeader(); animate bonus pulse on shot counter
│   ├─ sinkShip(ship)
│   │     all ship.cells → setCellState(.., 'sunk'); shipsSunk++
│   │     showToast(`🔥 ${ship.name} SUNK!`)
│   │     checkWin()
│   └─ checkWin()
│         if cellsHit === TOTAL_SHIP_CELLS: endGame('win')
├─ endGame(reason)   ('win' | 'no_shots' | 'timeout')
│     clearInterval(timerHandle); elapsedS = timeLimitS - remainingS
│     revealAllShips() after 600 ms; renderEnd(reason)
├─ revealAllShips()
│     for each unsunk ship cell: setCellState(v,h,'hit') with no animation
├─ Timer
│   └─ onTick()
│         remainingS--; elapsedS++; updateHeader()
│         if remainingS === 0: endGame('timeout')
├─ Header updater
│   └─ updateHeader()  → updates shot count, streak counter, timer display,
│                         ship-status segment colours
├─ Screen renderers
│   ├─ renderSetup()    → fleet briefing + launch button
│   ├─ renderPlaying()  → buildGrid(); set up input listeners; start timer
│   ├─ renderEnd(reason)→ 'win' | 'no_shots' | 'timeout'
│   └─ renderError(msg)
└─ Credit claim
    └─ claimCredits()   → same POST flow as Activities 1–3
```

### CSS Architecture

```
:root           → colour vars (--bg #001220, --grid-bg #001a2e, --cell-border #003828,
                  --cell-hit #c04010, --cell-miss #002040, --cell-sunk #e83020,
                  --cell-targeted-glow #00ff88, --accent #4af, --warn #ff4444, ...)
body            → dark navy bg; flex column; safe-area insets applied
.header-bar     → flex row; flex-shrink: 0; dark bg; bottom border; gap 0.6em
.ship-seg       → inline-block 8×8 px coloured square; border-radius 2px
                  .ship-seg.hit  → background: #222
                  .ship-seg.sunk → background: #f44; opacity 0.5
.grid-wrap      → flex: 1 1 0; min-height: 0; overflow: hidden; position: relative
.radar-overlay  → position: absolute; inset: 0; conic-gradient sweep; pointer-events: none
.grid           → display: grid; grid-template-columns: 1.8em repeat(10, 1fr);
                  grid-template-rows: 1.4em repeat(10, 1fr)
.col-label      → small text; text-align: center; color: #4af; font-size: 0.75em
.row-label      → small text; text-align: right; padding-right: 4px; color: #4af; font-size: 0.75em
.cell           → aspect-ratio: 1; border: 1px solid var(--cell-border); cursor: pointer;
                  display: flex; align-items: center; justify-content: center;
                  font-size: 1.1em; transition: background 100ms
.cell:hover     → background: #003040 (unknown cells only)
.cell.targeted  → animation: targetPulse 1.2s infinite
.cell.hit       → background: #c04010; cursor: default; animation: hitFlash 500ms forwards
.cell.miss      → background: #002040; cursor: default; animation: missRipple 400ms
.cell.sunk      → background: #e83020; cursor: default; animation: sunkPulse 400ms 3
.input-strip    → flex row; flex-shrink: 0; flex-wrap: nowrap; align-items: center;
                  gap: 0.4em; padding: 0.5em 0.6em; background: rgba(0,8,18,0.95);
                  border-top: 1px solid #003028
.input-strip label → font-size: 0.9em; color: #4af
.coord-input    → type=number; width: 3em; height: 44px; text-align: center;
                  background: #001a2e; color: #fff; border: 1px solid #4af
.ans-input      → type=text; width: 4em; height: 44px; text-align: center;
                  background: #001a2e; color: #fff; border: 1px solid #4af
.fire-btn       → min-height: 44px; min-width: 80px; background: #c04010; color: #fff;
                  border: none; border-radius: 4px; font-weight: bold
.fire-btn:disabled → opacity: 0.4; cursor: not-allowed
.warn-msg       → font-size: 0.75em; color: #ff8844; padding: 2px 6px;
                  visibility: hidden  (toggled to visible when needed)
.input-strip.shake → animation: shake 380ms
.shot-counter.bonus → animation: bonusPulse 350ms
.toast          → position: fixed; top: 10px; left: 50%; transform: translateX(-50%);
                  background: rgba(0,0,0,0.85); color: #ff8844; padding: 6px 14px;
                  border-radius: 20px; font-weight: bold; z-index: 100;
                  animation: toastSlide 2000ms forwards
.setup-screen   → centred flex column; height: 100%; dark navy bg
.fleet-legend   → list of ships; each row: icon + segments + label
.fleet-row      → flex row; align-items: center; gap 0.5em; margin 0.3em 0
.end-screen     → centred flex column; height: 100%; dark navy bg
@keyframes hitFlash, missRipple, sunkPulse, targetPulse
@keyframes radarSweep
@keyframes shake
@keyframes bonusPulse
@keyframes toastSlide
```

### Acceptance Criteria

- [ ] File is a valid HTML5 document with no external resource references.
- [ ] Uncompressed file size < 200 KB (target); must not exceed 400 KB.
- [ ] Compressed size < 100 KB (verify with `gzip -9`).
- [ ] Source code is human-readable; no minification, no obfuscation.
- [ ] `<title>` is `"Battle Math: Grid Commander"`.
- [ ] Missing/zero URL params → ERROR screen; game does not start.
- [ ] Valid URL params → SETUP screen shown before game logic runs.
- [ ] SETUP screen shows fleet briefing (all 6 ships with sizes) and `[ Launch Torpedoes! ]` button.
- [ ] `[ Launch Torpedoes! ]` button is always enabled.
- [ ] Pressing `[ Launch Torpedoes! ]` transitions to PLAYING; timer starts; grid renders.
- [ ] Grid is 10 × 10; V labels 1–10 on left; H labels 1–10 on top.
- [ ] Ship placement: all ships within grid; no overlaps; 1-cell buffer between ships.
- [ ] Ships placed randomly — positions differ between sessions.
- [ ] Tapping an unrevealed cell pre-fills V and H inputs; focuses answer input; cell gains `.targeted`.
- [ ] Typing valid V and H values in the inputs highlights the corresponding cell with `.targeted`.
- [ ] `.targeted` moves to the newly indicated cell whenever V or H changes to a valid value.
- [ ] `.targeted` is removed when V or H is cleared or invalid.
- [ ] FIRE button disabled when V or H is empty/invalid.
- [ ] FIRE button disabled when answer is empty.
- [ ] FIRE button disabled when cell (V, H) is already revealed; warn-msg visible.
- [ ] Warn-msg hidden in all other states.
- [ ] Tab key advances focus: V → H → answer.
- [ ] Enter on V focuses H; Enter on H focuses answer; Enter on answer triggers FIRE.
- [ ] FIRE with correct answer (ans == V×H) and unknown cell: cell revealed; no shot lost.
- [ ] FIRE with correct answer, hit: cell → `.hit` with `hitFlash`; `cellsHit++`; ship hitCount updated.
- [ ] FIRE with correct answer, miss: cell → `.miss` with `missRipple`; shot count unchanged.
- [ ] FIRE with wrong answer (ans ≠ V×H): `shotsRemaining--`; `streakCount = 0`; input-strip `.shake` plays; answer cleared; no cell revealed.
- [ ] `streakCount` increments on each correct answer.
- [ ] At `streakCount == 3`: `shotsRemaining++`; `streakCount = 0`; `.shot-counter.bonus` animation plays; header updated.
- [ ] Shot count displayed in header updates after every gain/loss.
- [ ] Streak counter displayed in header updates correctly (0–2, resets to 0 on bonus or wrong).
- [ ] Ship fully sunk: all its cells transition from `.hit` to `.sunk` with `sunkPulse`; `shipsSunk++`; SUNK toast displayed.
- [ ] SUNK toast shows ship name; disappears after 2000 ms.
- [ ] `shotsRemaining == 0` → `endGame('no_shots')` immediately.
- [ ] Timer reaches 0 → `endGame('timeout')` immediately.
- [ ] All 19 cells hit → `endGame('win')` immediately.
- [ ] All ship positions revealed on the grid 600 ms after END screen renders.
- [ ] WIN screen: "FLEET DESTROYED! 🎉" title; ships sunk count; shots used; time remaining; credits as h:mm:ss.
- [ ] GAME OVER (no shots) screen: "OUT OF AMMO! 💣" title; ships sunk; cells hit / 19; credits.
- [ ] GAME OVER (timeout) screen: "TIME'S UP! ⏱" title; ships sunk; cells hit / 19; credits.
- [ ] `earnedCredit = Math.floor(credit_s * cellsHit / 19)`.
- [ ] `submitCredit = Math.min(earnedCredit, credit_s)`.
- [ ] Credits submitted automatically on END screen; "Saving your credits…" shown while in flight.
- [ ] HTTP 200 → green banner "Credits added! Counter: `new_counter_hms`".
- [ ] HTTP 403 → red non-retriable banner; no retry button.
- [ ] HTTP 429 → orange non-retriable banner; no retry button.
- [ ] Other HTTP or network error → red banner with **Try again** button (retriable).
- [ ] Radar sweep animation runs continuously on `.grid-wrap` during PLAYING.
- [ ] `targetPulse` animation runs on the `.targeted` cell.
- [ ] `hitFlash` plays on a cell transitioning to `.hit`.
- [ ] `missRipple` plays on a cell transitioning to `.miss`.
- [ ] `sunkPulse` plays on cells transitioning to `.sunk`.
- [ ] No `var`, no `eval()`, no `alert()/confirm()`, no external resources.
- [ ] `<meta name="viewport">` includes `maximum-scale=1.0` and `user-scalable=no`.
- [ ] Layout is single-column (header / grid-wrap / input strip) in BOTH portrait and landscape.
- [ ] No horizontal scrollbar at 320 px wide in portrait.
- [ ] No horizontal scrollbar at 568 × 320 px in landscape.
- [ ] `env(safe-area-inset-*)` padding applied to `body`.
- [ ] Minimum cell size 28 px enforced; cells remain tappable on 320 px viewport.
- [ ] Input strip elements (`V`, `H`, answer, FIRE) fit on one line at 320 px with `clamp` font size.

### Required `POST /api/activities/credit` Fields

```javascript
{
  device_idx:        deviceIdx,    // int
  act_id:            actId,        // int
  credits_s:         submitCredit, // Math.floor(creditsS * cellsHit / 19), capped at creditsS
  completion_time_s: elapsedS,     // seconds elapsed since game start
  pin:               pin,          // string from URL
  token:             token         // string from URL
}
```

---

## Phase A4.3 — Integration & Verification (Activity 4)

### Goal

Register `battle_math.html` as a dynamic activity in the firmware, wire it to a test
device, and verify the complete play-through flow end-to-end across all three game
outcomes (WIN, out-of-ammo GAME OVER, timeout GAME OVER) and all edge cases.

### Inputs

- Phase A4.2 output: `main/dyn_activities/battle_math.html`.
- Existing Feature 8 infrastructure (Phases 8.1–8.5 must be complete).

### Tasks

1. **Build**: run `idf.py reconfigure && idf.py build`.  The CMake glob picks up
   `battle_math.html` automatically; `g_dyn_act_count` increments by 1.

2. **Create activity via admin UI**:
   - Navigate to `/activities/manage`.
   - Tick "Dynamic activity".
   - Select `battle_math` from the combobox.
   - Set **Credit** to `0:10:00` (600 s), **Time Limit** to `0:05:00` (300 s),
     **Daily Limit** to `2`.
   - Submit → HTTP 303 redirect.

3. **Assign to device 0** via the same manage page.

4. **Kid workflow test — WIN path**:
   - Navigate to `/dyn` from device 0's browser.
   - Verify device 0's nickname and the `battle_math` button appear.
   - Click the button; confirm URL contains `pin`, `device_idx=0`, `act_id`,
     `credits_s=600`, `time_limit_s=300`, `token`.
   - Game loads; verify SETUP screen shows fleet briefing.
   - Press **Launch Torpedoes!**; verify 10×10 grid renders with V and H labels.
   - Play through until all 6 ships are sunk (WIN state triggered).
   - Verify WIN screen: "FLEET DESTROYED!" + correct credits; grid reveals all ships.
   - Verify green "Credits added!" banner and `new_counter_hms` after claim POST.

5. **GAME OVER — out of ammo path**:
   - Start a new game; deliberately enter wrong answers until `shotsRemaining = 0`.
   - Verify GAME OVER screen "OUT OF AMMO!" with proportional credit.
   - Verify claim POST accepted; green banner shown.

6. **GAME OVER — timeout path**:
   - Create activity with `time_limit_s=30` (30 s); play slowly until timer reaches 0.
   - Verify GAME OVER screen "TIME'S UP!" with proportional credit.

7. **Correct-answer no-shot-loss test**:
   - Fire at an empty (miss) cell with a correct answer; verify `shotsRemaining`
     unchanged, cell marked as miss, streak increments.

8. **Wrong-answer shot-loss test**:
   - Enter a wrong answer; verify `shotsRemaining--`; streak resets to 0; input-strip
     shakes; no cell is revealed; answer input cleared.

9. **Streak bonus test**:
   - Enter 3 correct answers in a row; verify `shotsRemaining++` on the third, shot
     counter animates, streak resets to 0.
   - Enter a correct answer then a wrong answer; verify streak resets; no bonus awarded.

10. **Already-targeted cell test**:
    - Reveal a cell; type V and H matching that cell; verify FIRE button is disabled
      and warn-msg is visible.  Verify no shot is lost on attempted FIRE.

11. **Tap-to-target test**:
    - Tap an unrevealed cell; verify V and H inputs are pre-filled and answer input
      receives focus.
    - Tap a revealed (hit/miss) cell; verify inputs are NOT changed.

12. **Ship placement variety test**:
    - Reload the game 5 times; verify ship positions differ between sessions (ships
      are randomised, not fixed).

13. **SUNK toast test**:
    - Sink a ship; verify the correct ship name appears in the toast notification; toast
      disappears after ~2000 ms.

14. **Grid reveal on END test**:
    - Trigger GAME OVER; verify all ship positions are revealed on the grid ~600 ms
      after the END screen renders.

15. **Replay attack test**: after claiming, re-submit same POST (same `token`) →
    server must return HTTP 403; non-retriable banner shown.

16. **Daily limit test**: claim twice (daily limit = 2); third attempt → HTTP 429 banner.

17. **Missing param test**: manually load URL with `token` omitted → ERROR screen;
    game does not start.

18. **Responsiveness test**: load game on a 320 px portrait viewport (browser DevTools);
    full 10×10 grid, header, and input strip must all be visible without horizontal scroll.

19. **Landscape test**: rotate device or set DevTools to 568 × 320 px landscape;
    verify grid adapts to fill the taller space; input strip remains at bottom; no layout
    breakage.

20. **Build artefact check**: `g_dyn_act_registry` must include an entry with
    `p_name == "battle_math"`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings.
- [ ] `g_dyn_act_count` increases by 1 compared to pre-game build.
- [ ] `GET /dyn_activities/battle_math` returns HTTP 200 with `Content-Type: text/html`.
- [ ] `GET /dyn_activities/battle_math.html` returns HTTP 200 (`.html` extension handled).
- [ ] `/dyn` page shows `battle_math` button for registered device.
- [ ] Launch URL contains all 6 required parameters including `time_limit_s`.
- [ ] WIN flow: all 19 cells hit → WIN screen → POST 200 → green banner.
- [ ] Out-of-ammo GAME OVER: proportional credits posted and accepted.
- [ ] Timeout GAME OVER: proportional credits posted and accepted.
- [ ] Correct answer on miss: no shot lost; cell marked miss; streak increments.
- [ ] Wrong answer: shot lost; streak reset; no cell revealed; shake plays.
- [ ] Streak 3 correct: bonus shot added; streak resets to 0.
- [ ] Already-targeted cell: FIRE disabled; warn-msg visible; no shot lost.
- [ ] Tap-to-target: V and H pre-filled; answer focused; `.targeted` class applied.
- [ ] Ship placement is random across sessions.
- [ ] SUNK toast shows correct ship name; disappears after 2 s.
- [ ] All ships revealed on grid 600 ms after END screen.
- [ ] Replay of consumed token → HTTP 403 banner.
- [ ] Third daily claim → HTTP 429 banner.
- [ ] Missing URL param → ERROR screen (game does not start).
- [ ] No horizontal scroll at 320 px portrait or 568 × 320 px landscape.
- [ ] No regression in Activities 1–3 tests or Phases 8.1–8.5 criteria.

---

## Dependency Notes

- Feature 8, Phases 8.1–8.5 must be complete before Phases A1.3, A2.3, A3.3, and A4.3
  can run.
- Phases A1.2, A2.2, A3.2, and A4.2 (HTML files) are independent of firmware and can be
  developed and reviewed on a desktop browser without any firmware build.
- The `/dyn` page launch URL must include `time_limit_s` (Option A, Phase A1.1 §URL
  Parameters).  This requires a **one-line change** in `http_server_dyn.c` Phase 8.4
  task 2b: append `&time_limit_s=%lu` with `act_entry.time_limit_s` to the JS
  `navigateTo` URL string.  This change applies to all four activities and should be
  tracked as part of Phase A1.3 task 1 (or retrofitted into Phase 8.4 if not yet
  implemented).
- Phases A2.2 (`fish_math.html`), A3.2 (`shoot_math.html`), and A4.2
  (`battle_math.html`) are fully independent of each other and of Phase A1.2 — they
  can be developed in parallel.
- Phase A4.2 (`battle_math.html`) has no dependency on Activities 1–3 beyond borrowing
  the credit-claim POST pattern from `space_math.html` as a reference.  It can be
  developed independently at any time after Phase 8.5 is complete.

---

## Summary of Design Decisions

| Decision | Choice | Rationale |
| -------- | ------ | --------- |
| Seconds per problem | 5 | Research-backed estimate for an 8-year-old on simple arithmetic with on-screen keypad input |
| Normal-credit fraction | 70% of `time_limit_s` | Leaves 30% as bonus time; makes the win achievable but not trivially easy |
| Extra problems | Award proportional bonus shown on screen; submit capped at `credit_s` | Server always enforces cap; no server change needed; kid still feels rewarded |
| Partial credit on timeout | Proportional to `solved / targetProblems`; game is marked GAME OVER | Fair and non-punitive; motivates faster solving in future sessions |
| `time_limit_s` delivery | URL parameter (`&time_limit_s=N`) added by `/dyn` page | Simplest path; no extra API call at game start; informational-only, no security risk |
| Answer input | On-screen keypad + physical keyboard, `maxlength="2"` | Minimises time lost to input; answers ≤ 81 always fit in 2 chars |
| Max simultaneous meteors | 3 | Readable on a phone screen; avoids cognitive overload for age group |
| Wrong-answer penalty | −1 second from timer | Light penalty that teaches care without destroying morale |
| Meteor-hits-ground penalty | None (problem lost, no life system) | Reduces frustration; keeps game continuous |
| Problem types | Add (≤99), Sub (≥1), Mul 1-digit×1-digit | Age-appropriate; no division |
| Speed progression | 4 tiers: 12 s → 11 s → 10 s → 9 s per fall | Age-appropriate pace; early stage gives a child ~12 s to read and answer each problem |
| No sounds | Silent | Avoids browser permission prompts; suitable for shared/school environments |
| Graphics | Pure CSS/SVG | No external assets; keeps file size small |
| Credit submission | Automatic on game end; retry button for network errors only | Eliminates the risk of a child missing the "Claim Credits" button |
| Active meteor selection | Auto (lowest falling) + tap/click any meteor to switch | Lets the player skip a hard problem and solve an easier one first |
| **Activity 2** | | |
| Bubbles float upward | Direction reversed vs Activity 1 meteors | Visual novelty; thematically consistent with underwater setting; re-uses same spawn/speed logic |
| Clown fish SVG character | Inline SVG with CSS idle/happy/wrong animations | Zero flash overhead; adds engagement for the target age group without external assets |
| Fish reactions | Happy jump on correct, shake on wrong | Immediate expressive feedback; reinforces correct answers without distracting text overlays |
| Active bubble auto-selection | Bubble with minimum `topPx` (closest to top/escaping) | Same intuition as Activity 1 "lowest meteor" but direction-appropriate |
| Underwater background | Ocean gradient + rising CSS bubble layers + seaweed | Consistent with game theme; pure CSS; no image assets; replaces star-layer pattern from Activity 1 |
| Stat label | "Popped" instead of "Solved" | Thematically appropriate for bubble-popping; visually distinct from Activity 1 |
| **Activity 3** | | |
| Multiple-choice answer model | 3 asteroids = 1 correct + 2 distractors for 1 problem | One mental task at a time; no typing overhead; distractors add educational value |
| Plausible distractors | ±1–9 offset (add/sub); neighbouring times-table entry (mul) | Challenges recall rather than guessing; avoids trivially obvious wrong answers |
| Direction toggle (RTL / LTR) | SETUP radio; CSS `flex-direction: row-reverse` + SVG `scaleX(-1)` | Accommodates personal preference; implemented with a single CSS class flip |
| Row movement | Discrete snap (▲/▼); no smooth scroll | Cleaner for touch; avoids accidentally landing between rows; snap + pulse gives clear feedback |
| Tap asteroid = snap + fire | Single touch action selects and shoots | Fastest possible input on touch devices; consistent with "I know the answer" intuition |
| Re-fire on tried-wrong ignored | Silent ignore; no second penalty | Prevents frustration from accidental double-tap; rewards attention to the dimmed indicator |
| Collision = forced selection | Asteroid reaching rocket → evaluates rocket's current row | Guarantees every problem is resolved; no "escape" mechanic needed; keeps game tense |
| One active set at a time | No overlapping sets | Reduces cognitive load; clear one-problem-at-a-time focus for the target age group |
| Speed tiers | 8 s → 7 s → 6 s → 5 s (4 tiers) | Slightly faster than Activities 1/2 (no typing required); still age-appropriate |
| Flee animation on correct hit | `translateX(±150vw)` CSS transition, 350 ms | Satisfying visual reward; clears the screen quickly for the next set |
| Landscape primary | Horizontal play field suits horizontal movement natively | Natural orientation for a side-scrolling shooter; portrait supported as fallback |
| Star-field background | Reused from `space_math.html` | Consistent space theme; zero additional code cost |
| Stat label | "Shot" instead of "Solved" | Thematically appropriate for shooting mechanic |
| **Activity 4** | | |
| Multiplication only | 10×10 grid maps directly to the standard times table | The map and the math are the same object — knowing facts 1–10 unlocks the entire grid |
| Player-chosen target (V × H) | Player fills V, H, AND answer | Player must both select a strategic target AND prove knowledge of that fact; one mechanic rewards two skills simultaneously |
| No random problem generation | Problems are player-chosen, not randomly presented | Adds spatial strategy layer absent in Activities 1–3; replayability from random ship placement rather than random questions |
| Wrong answer = shot lost, no reveal | Shot consumed without firing | Directly rewards mathematical fluency; a child who knows all 100 facts never wastes shots on wrong answers |
| Correct answer, miss = no shot lost | Miss does not cost a shot | Separates "strategic mistake" (bad cell choice) from "mathematical mistake" (wrong answer); only math errors are penalised |
| 10 starting shots | Enough for strategic play; not enough for brute force (100 cells) | Forces the player to be selective and strategic, not exhaustive |
| Streak bonus shot (+1 per 3 correct) | Rewards consecutive correct answers | Encourages fluency; gives mathematically confident players more tactical flexibility |
| `TOTAL_SHIP_CELLS = 19` | Carrier(5)+Battleship(4)+2×Destroyer(3)+2×PatrolBoat(2) | Classic Battleship fleet; 19% grid coverage is enough to make random guessing inefficient |
| Credit = proportional to cells hit | `Math.floor(credit_s * cellsHit / 19)` | Cell-based credit is more granular than ship-based; every hit matters regardless of whether the ship sinks |
| Tap-to-target | Tap cell → pre-fills V and H; focuses answer input | Removes coordinate typing overhead on mobile; player focuses purely on the multiplication fact |
| Single-column layout (no right panel) | Header / grid / input strip stacked vertically | Maximises grid size in both orientations; no horizontal space split needed for a turn-based game |
| `aspect-ratio: 1` CSS grid cells | Grid auto-scales in both orientations without JS | Uniform square cells at any viewport size; no JS resize listener needed |
| Bottom input strip (3 inputs in a row) | `V [input] × H [input] = [input] [FIRE!]` | Minimal vertical footprint; keeps grid large; familiar calculator-style left-to-right flow |
| Disabled FIRE for already-shot cells | FIRE button disabled + warn-msg | Prevents accidental shot waste on known cells; cleaner than silent ignore |
| Ship placement 1-cell buffer | No two ships touch (including diagonals) | Avoids ambiguous partial-sink scenarios where adjacent ships share a border; makes each SUNK event unambiguous |
| Radar sweep overlay (CSS conic-gradient) | Rotating overlay at low opacity on `.grid-wrap` | Sonar/radar aesthetic at zero JS cost; `pointer-events: none` so it never intercepts taps |
| SUNK toast notification | Fixed-position sliding toast with ship name | Immediate gratifying feedback; does not disrupt the grid or require the player to dismiss it |
| Grid revealed on END | All ship positions shown 600 ms after END screen | Satisfying reveal moment; helps the child learn where ships were and why they missed |
| Relaxed file size budget | < 100 KB compressed (vs ~10–15 KB for Activities 1–3) | Richer SVG assets and more CSS animation detail justified for a visually richer game; larger budget approved by project |

/*** end of file ***/
