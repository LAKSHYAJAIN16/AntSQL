---
name: AntSQL Cloud
description: A document database that heals itself.
colors:
  electric-indigo: "#5a3fff"
  indigo-pressed: "#4429e0"
  ink: "#15121f"
  body-gray: "#4c4a5c"
  muted-gray: "#6e6b82"
  paper-white: "#ffffff"
  hairline: "#e7e3f4"
  hairline-strong: "#d8d2ee"
  proof-band: "#130d2b"
  band-text: "#f2effc"
  band-muted: "#ada7d1"
  band-hairline: "#322a5c"
  syntax-keyword: "#b6a2ff"
  syntax-string: "#7fd8b0"
  status-good: "#1fae72"
  status-bad: "#e0434c"
typography:
  display:
    fontFamily: "Archivo, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif"
    fontSize: "clamp(2.4rem, 5.6vw, 4.4rem)"
    fontWeight: 900
    lineHeight: 1.03
    letterSpacing: "-0.035em"
  headline:
    fontFamily: "Archivo, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif"
    fontSize: "1.9rem"
    fontWeight: 800
    letterSpacing: "-0.025em"
  body:
    fontFamily: "Archivo, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.55
  code:
    fontFamily: "Spline Sans Mono, SFMono-Regular, Consolas, Liberation Mono, Menlo, monospace"
    fontSize: "0.86rem"
    fontWeight: 400
rounded:
  pill: "999px"
  panel: "16px"
  code: "14px"
  field: "8px"
spacing:
  xs: "8px"
  sm: "16px"
  md: "22px"
  lg: "40px"
  xl: "80px"
components:
  button-primary:
    backgroundColor: "{colors.electric-indigo}"
    textColor: "{colors.paper-white}"
    rounded: "{rounded.pill}"
    padding: "10px 20px"
  button-primary-hover:
    backgroundColor: "{colors.indigo-pressed}"
  button-outline:
    backgroundColor: "transparent"
    textColor: "{colors.ink}"
    rounded: "{rounded.pill}"
    padding: "10px 20px"
---

# Design System: AntSQL Cloud

## Overview

**Creative North Star: "The Verified Ledger"**

AntSQL Cloud earns belief the way its own colony demo does: by showing a state change the
visitor caused, not by asserting quality. The world is Fauna's real, verified 2024 marketing
system (captured from `web.archive.org/web/20240102090829/https://fauna.com/`, not assumed from
memory) — crisp white ground, one full-bleed deep-indigo band that interrupts the page exactly
once for the moment that needs weight, bold black Archivo display type, and dark violet-black
code/terminal panels threaded through the light sections. Numbers that look like Fauna's investor
stats are real, verifiable facts about this specific codebase (shard count, replica count, passing
test count), never vanity metrics.

The system commits to a Restrained-to-Committed color strategy: neutrals and ink carry the page;
electric indigo is reserved for action (buttons, links, active data) and for owning the one dark
band at full saturation. It never appears as ambient decoration.

**Key Characteristics:**
- White ground interrupted exactly once by a full-bleed indigo-black "proof band"
- Bold, black Archivo display type; no serif, no script, no system-font default
- Dark violet-black code and terminal panels with indigo/mint syntax accents, embedded in
  otherwise-light sections
- A thin accent-colored sine-wave divider marks the one transition from light content into the
  dark proof band
- No card grids; feature comparisons are rule-separated editorial lists
- Live state changes (a replica failing or healing) get a visible flip transition, not a silent
  re-render — borrowed discipline from a split-flap departure board, applied only to the Live
  Colony status rows

## Colors

Two-register palette: a quiet white-and-ink register for reading, and one saturated indigo-black
register reserved for the proof band and code panels.

### Primary
- **Electric Indigo** (`#5a3fff`): every call-to-action button, link color, active pheromone bar
  fill, focus ring, selection highlight. Never used as a large background fill outside the proof
  band.

### Neutral
- **Paper White** (`#ffffff`): page background throughout every light section.
- **Ink** (`#15121f`): headings, primary body strong text, code-panel replica identifiers.
- **Body Gray** (`#4c4a5c`): paragraph copy.
- **Muted Gray** (`#6e6b82`): secondary/caption text (footer, shard labels) — darkened from a
  first-pass `#85829a` after measuring contrast at 3.71:1 against white; `#6e6b82` measures
  5.14:1, clearing WCAG AA for normal text.
- **Hairline** (`#e7e3f4`) / **Hairline Strong** (`#d8d2ee`): all borders and dividers; hairline
  strong is reserved for interactive-element borders (inputs, outline buttons) so they read as
  slightly more present than a passive divider.

### The Proof Band
- **Proof Band** (`#130d2b`): the one full-bleed dark section and every code/terminal panel
  elsewhere on the page — the two contexts share one background so a visitor learns "dark
  violet-black panel = real system output" once and reads it everywhere.
- **Band Text** (`#f2effc`) / **Band Muted** (`#ada7d1`): text and secondary labels on the band.
- **Band Hairline** (`#322a5c`): borders on code panels.
- **Syntax Keyword** (`#b6a2ff`) / **Syntax String** (`#7fd8b0`): hand-applied syntax accents in
  the two illustrative code samples (curl is left unhighlighted; the SDK example is highlighted
  since it has more structure worth reading at a glance).

### Status
- **Status Good** (`#1fae72`) / **Status Bad** (`#e0434c`): replica alive/dead pills only. Kept
  outside the indigo system deliberately — status color must never be mistaken for brand color.

### Named Rules
**The One Dark Rule.** The full-bleed proof-band background exists in exactly one place per page
load (the stats band). Code and terminal panels reuse its color as a *component*, not a second
full-bleed section — if a build ever wants a second full-bleed dark section, that's a system
decision, not a local one.

## Typography

**Display/Body Font:** Archivo (400–900), with a system-sans fallback stack
**Code Font:** Spline Sans Mono (400–600)

**Character:** Archivo is a confident, black-weight-capable grotesk with blocky terminals — the
closest obtainable free match to the bold, high-contrast black display headlines in the verified
Fauna evidence, without repeating Space Grotesk, IBM Plex, or the other faces this project has
already used and moved past. Spline Sans Mono is a geometric mono from the same design lineage as
Archivo (both are Indian Type Foundry commissions distributed via Google Fonts), chosen so display
and code read as one family of hand, not two unrelated stacks glued together.

### Hierarchy
- **Display** (900, `clamp(2.4rem, 5.6vw, 4.4rem)`, line-height 1.03): the hero `<h1>` only.
- **Headline** (800, `1.9rem`): section `<h2>`.
- **Title** (700–800, `1.05–1.7rem`): showcase/feature-list `<h3>`, colony shard labels.
- **Body** (400, `1–1.2rem`, line-height 1.55): paragraphs; hero lead runs up to 60ch, section
  subheads stay under 640px container width.
- **Label** (600, `0.7–0.72rem`, uppercase, 0.06em tracking): shard headers and status pills.

### Named Rules
**The One Family Rule.** Archivo carries every weight from body copy to display; no second
display face is introduced for "emphasis" — emphasis comes from weight (700–900) and size, never
a face change.

## Layout

Single-column content capped at `1180px`, with generous `clamp()`-based side padding
(`clamp(16px, 5vw, 64px)`) so the same rhythm holds from a 390px phone to a wide desktop. The
`.showcase` (code-alongside-copy) and `.band .stats` (three proof numbers) grids collapse to one
column at `860px` and `700px` respectively; `.feature-list` and `#playground .grid` collapse at
`640px`/`800px`. Section vertical rhythm uses `clamp(48px, 8vw, 80px)` top/bottom padding
throughout, so every section breathes at the same rate regardless of viewport.

## Elevation & Depth

Flat by design, matching the verified source evidence: panels and code blocks are distinguished by
a 1px hairline border and a background-color shift, never a drop shadow. The only non-flat
treatment is the proof band's own full-bleed color field, which reads as depth through contrast
rather than through a shadow.

### Named Rules
**The Flat-By-Default Rule.** No `box-shadow` exists anywhere in this system. If a future
component genuinely needs elevation (a dropdown, a toast), it should still prefer a border/color
shift first; a shadow is the last resort, not the default reach.

## Shapes

Two radii only: **pill** (`999px`, every button) and **panel** (`16px` panels, `14px` code
blocks — code is intentionally one step tighter so it reads as a distinct, denser object type from
a content panel). Form fields use a small `8px` radius, closer to square, so they read as
functional controls rather than decorative surfaces.

## Components

### Buttons
- **Shape:** full pill (`border-radius: 999px`).
- **Primary (`.btn-solid`):** electric indigo background, white text, 700 weight. Hover darkens to
  `#4429e0`; active nudges down 1px (`translateY(1px)`) rather than scaling, so it reads as a
  press, not a pop.
- **Outline (`.btn-outline`):** transparent background, hairline-strong border, ink text. Hover
  swaps the border to indigo and the text to indigo — never fills the background, keeping outline
  buttons visually lighter than solid ones at every state.
- **Disabled:** `opacity: 0.4`, `cursor: not-allowed`, no color change beyond opacity — used on the
  Playground's Send/List/Refresh buttons until an API key exists.

### Panels / Containers
- **Corner:** `16px`.
- **Background:** white, 1px hairline border. No shadow (see Elevation & Depth).
- **Internal padding:** `22px`.

### Code / Terminal Panels
- **Corner:** `14px`.
- **Background:** proof-band color (`#130d2b`), band-hairline border.
- **Use:** every `<pre>` code sample, and the Playground's live `#output` response panel — the
  same visual object represents "example code" and "real live response," which is deliberate:
  it tells the visitor the response panel is not a mock.

### Inputs / Fields
- **Style:** `1px` hairline-strong border, `8px` radius, off-white-tinted fill (`#fbfaff`), mono
  type (inputs hold identifiers and JSON, not prose).
- **Focus:** border shifts to electric indigo; caret color is also indigo.
- **Disabled:** not used on inputs in the current build.

### Live Colony Status Rows (signature component)
Each shard is a bordered panel; each replica is a row with an alive/dead pill, a pheromone-
strength bar (`transform: scaleX()`, never `width`, to stay off the main thread), and fail/heal
controls. When a row's alive/dead state actually changes between two stats refreshes, it plays a
0.55s flip (`scaleY` + opacity, `rowFlip` keyframes) instead of silently re-rendering — the one
piece of borrowed system discipline in this build, adapted from a split-flap departure board's
"state changes are events" grammar. First render never flips (there is nothing to transition
from); only a real transition does.

## Do's and Don'ts

### Do:
- **Do** reuse the proof-band color (`#130d2b`) for every code/terminal surface, never invent a
  second dark tone.
- **Do** keep electric indigo reserved for action and status-of-interest (buttons, links, active
  bars) — if indigo starts covering large passive surfaces outside the one proof band, that is
  brand dilution, not embellishment.
- **Do** measure new text-color pairings against white/band backgrounds before shipping (see the
  muted-gray correction above) rather than eyeballing contrast.
- **Do** keep the Live Colony flip transition tied to a real state change; never fire it on a
  routine re-render.

### Don't:
- **Don't** introduce a card grid (icon + heading + text, equal-size boxes) for feature
  comparisons — this system's answer is the rule-separated editorial list.
- **Don't** add a kicker/eyebrow label above a heading.
- **Don't** add a second display typeface; Archivo carries the whole hierarchy.
- **Don't** add drop shadows; depth comes from the hairline-border-plus-fill-color language only.
- **Don't** fabricate stat-band numbers. Every big numeral in the proof band must be independently
  verifiable from the codebase or its test output.
