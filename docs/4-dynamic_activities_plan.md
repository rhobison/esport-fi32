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
| 1 | Space Meteor Shower | `space_math.html` | Specified | A1.1, A1.2, A1.3 |
| 2 | Clown Fish Bubble Burst | `fish_math.html` | Specified | A2.1, A2.2, A2.3 |

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

The game has four mutually exclusive screen states managed by a JS state machine:

| State       | Trigger                               | Description                                           |
| ----------- | ------------------------------------- | ----------------------------------------------------- |
| `LOADING`   | Page load                             | Validates URL params; transitions to `PLAYING` or `ERROR` |
| `ERROR`     | Missing/invalid URL params            | Shows error message + back link                       |
| `PLAYING`   | After `LOADING` succeeds              | Main game loop                                        |
| `END`       | Timer reaches 0 OR `solved >= targetProblems` | Shows WIN or GAME OVER result + Claim Credits button |

All states are rendered in the **same single `<div id="screen">`** by swapping its
`innerHTML` — no multi-page navigation.

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
├─ State variables
│   ├─ pin, deviceIdx, actId, creditsS, timeLimitS, token  (from URL params)
│   ├─ targetProblems, solved, wrong, elapsedS
│   ├─ gameState: 'loading' | 'playing' | 'end'
│   ├─ meteors[]  (array of active meteor objects)
│   └─ activeMeteorId  (id of the currently-answered meteor)
├─ Problem generator
│   └─ generateProblem() → { question: string, answer: number, type: string }
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
.end-screen     → centred; star-burst background animation on WIN
```

### Acceptance Criteria

- [ ] File is a valid HTML5 document with no external resource references.
- [ ] File size ≤ 40 KB uncompressed (target); must not exceed 80 KB under any circumstances.
- [ ] Source code is human-readable; no minification, no obfuscation.
- [ ] Missing/zero URL params → ERROR screen shown; game does not start.
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
| `LOADING` | Page load                                     | Validates URL params; transitions to `PLAYING` or `ERROR` |
| `ERROR`   | Missing/invalid URL params                    | Shows error message + back link                           |
| `PLAYING` | After `LOADING` succeeds                      | Main game loop                                            |
| `END`     | Timer reaches 0 OR `solved >= targetProblems` | WIN or GAME OVER result; credit claim auto-triggered      |

Same single `<div id="screen">` swap pattern as Activity 1.

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
│   ├─ targetProblems, solved, wrong, elapsedS
│   ├─ gameState: 'loading' | 'playing' | 'end'
│   ├─ bubbles[]  (array of active bubble objects)
│   ├─ activeBubbleId  (id of the currently-targeted bubble)
│   └─ fishHappyTimer, fishWrongTimer  (timeout handles for fish animation reset)
├─ Problem generator
│   └─ generateProblem()  (identical to Activity 1)
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
.end-screen     → centred; reef-burst glow animation on WIN
```

### Acceptance Criteria

- [ ] File is a valid HTML5 document with no external resource references.
- [ ] File size ≤ 40 KB uncompressed (target); must not exceed 80 KB under any circumstances.
- [ ] Source code is human-readable; no minification, no obfuscation.
- [ ] `<title>` is "Fish Math: Bubble Reef".
- [ ] Missing/zero URL params → ERROR screen shown; game does not start.
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

## Dependency Notes

- Feature 8, Phases 8.1–8.5 must be complete before Phases A1.3 and A2.3 can run.
- Phases A1.2 and A2.2 (HTML files) are independent of their respective integration
  phases and can be developed and reviewed on a desktop browser without firmware.
- The `/dyn` page launch URL must include `time_limit_s` (Option A, Phase A1.1 §URL
  Parameters).  This requires a **one-line change** in `http_server_dyn.c` Phase 8.4
  task 2b: append `&time_limit_s=%lu` with `act_entry.time_limit_s` to the JS
  `navigateTo` URL string.  This change applies to both activities and should be tracked
  as part of Phase A1.3 task 1 (or retrofitted into Phase 8.4 if not yet implemented).
- Phase A2.2 (`fish_math.html`) shares the same infrastructure as Phase A1.2 and can be
  developed independently or in parallel with Activity 1 phases.

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

/*** end of file ***/
