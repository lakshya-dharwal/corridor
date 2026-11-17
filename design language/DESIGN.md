# Corridor design language

This is the visual source of truth for the Corridor demo page (`web/index.html`). It records the idea, the direction, the exact values, and why each decision is there. Use it when a new screen, a new page, or a different product should look like Corridor.

The test for any new screen is the same one this page was built to pass. Squint at a screenshot. You should see one object, one headline, and one colour. If you see more, cut.

The page this document describes is a desktop briefing. It was checked in a desktop browser with live data. The phone rules exist so the instrument does not fall apart, and the canonical composition is the wide two-column scene.

---

## 1. What the page is for

Corridor briefs one flight. Give it two airports and it returns the route, an estimated blood-oxygen curve, the FAA supplemental-oxygen finding (14 CFR 91.211), a fatigue score, the weather at both ends, the forecast at arrival, and the hazard areas that actually contain the airplane.

The page has to make that pile of facts readable by someone who is not sitting in front of a dashboard legend. A saturation percentage, a regulation, a METAR, and a turbulence polygon all want to shout. The design gives each fact its own viewport and one picture that is the fact.

The reading order is the flight:

| Scene | Name | The one fact |
|---|---|---|
| 01 | Blood | Lowest estimated saturation, as a count of lit dots |
| 02 | Track | Distance and duration, as one ordered line |
| 03 | Altitude | Cruise altitude, as a climb that becomes an outline |
| 04 | Drop | How long the blood stays under 90% |
| 05 | Rule | Whether 91.211 requires oxygen, for crew and for passengers |
| 06 | Fields | The two airports, and the category when you land |
| 07 | Reach | How many hazard areas contain the airplane, not just the route |
| 08 | Corridor | Where along the map the blood is low, and where the hazards sit |

Scroll is the flight. The strip fixed to the bottom samples the altitude profile at the scroll position, so the numbers change because you moved through the route, not because something animated.

The voice on the page is short on purpose. A kicker names the scene. A giant word or number is the answer. One sentence says what the object means. A line of mono type carries the caveat, the source clock, or the raw observation. The educational disclaimer lives in that fine role, under the form on the first scene: an estimate for a healthy adult at rest, not medical advice, not for flight planning. It is present and it does not compete with the headline.

## 2. The idea

Dark, calm, and confident. One hero object, big words, very little else. It should look like an instrument, not a web page.

An instrument has a face, a reading, and a scale. It does not have a sidebar, a card grid, a five-item legend, or a chart that needs to be explained. Corridor’s face is a near-black field with hairline marks. Its readings are giant numbers and lattices of dots. Its scale is the tick ruler, the scene counter, and the scrub along the bottom.

The feeling to hit is “intentionally designed.” A visible step in clarity, not a coat of polish on a dashboard. The first version of this page was a light dashboard. It was replaced because five colours, several charts, and every fact at once made the briefing harder to see.

Three references shaped it. Take the structural ideas. Leave the decoration behind.

**Babylon** ([babylon.bio](https://babylon.bio)) is the structural reference. One idea per screen. Simple scrolling. Big text. A picture a person can count, in the spirit of a field of 100 dots with a handful lit. You do not read a legend. You see the count.

**Somnium** is where the accent comes from. On that project, orange-red means blood. Corridor keeps that meaning. The accent is blood oxygen and the stretch of the route where saturation is under 90%. It is not a brand decoration and it is not a link colour.

**Dark HUD dashboards, dotted globes, glass panels, 3D anatomy, planet arcs** were in the mood references. What survived from them is the instrument detailing: hairlines, corner marks, a tick ruler, small mono labels, one soft glow. What did not survive is glass on every panel, glow on every edge, a dotted globe for its own sake, purple haze, and 3D objects. Those read as a template once the content is a number.

## 3. Visual direction

The page is a black field. Warm off-white type sits on it. One orange-red appears when the subject is blood, or when a status is actually bad. Green and gold appear only as flight category, feed health, or a clear finding. Everything else is a hairline, a dot, or a word.

The field is flat. `#05060a` is painted on both `html` and `body`, so a scroll bounce does not flash white. There is no grey wash, no purple gradient, no noise texture, and no second background colour behind cards. The only atmosphere is one radial glow, fixed in the top-right corner, in the accent at low opacity, bleeding off the viewport. It warms the corner. It does not colour the page.

Objects are built from order. Dots sit on a regular lattice. A lattice looks designed. A random scatter looks like noise. The near side of a form is solid. The far side fades. The outline is brightened so the cluster reads as a shape, not as a cloud of equal specks.

Text never sits on the object. Words take the left column. The object takes the right, inside a quiet frame or as a pair of station names. If a future screen truly must overlap them, dim the object to about 10% and keep the type in `--ink`. That case does not exist on this page.

Glass and glow are rare. The bottom strip is a solid black bar with a hairline, because a blur over scrolling type looks like a filter. The glow exists once, in the corner, and nowhere else. Dots do not glow. Headlines do not glow. The map does not glow.

Live numbers do the work that animation would do. The bottom strip shows altitude, saturation, elapsed time, hazard state, and the oxygen rule, and those values track the scroll position through the profile. There is no entrance animation, no pulsing dot, no count-up. Scroll-snap is `proximity`, so a scene settles when you are near it and you can still stop between scenes. `prefers-reduced-motion` turns snap off.

Radii stay small. Frames, fields, and buttons are 6px. The map and map controls are 4px. A large radius reads as a template bubble. Nothing is a pill.

## 4. How the design creates clarity

Each decision below is a way of removing a competition.

**One fact per viewport.** A dashboard asks the eye to rank a chart, a table, a map, and a status pill at the same time. A scene asks it to read one headline and check it against one object. The oxygen rule is not beside the map. The map is the scene where the route and the hazards finally share a frame, and by then both have been introduced alone.

**The object is the number.** The hero lattice has 100 dots. The count that is lit is the saturation. “82 lit” and a giant 82 are the same fact in two forms, so a person who skips the sentence still has it. The drop scene’s dots grow with saturation and turn accent under 90, so the threshold is visible without a legend entry called “below 90%.”

**Colour is a claim.** Orange-red claims “this is blood” or “this is the bad state.” Green claims “this is fine” (VFR, live feed, clear of the hazard, oxygen not required). Gold claims “this is marginal” (MVFR, a degraded feed). A colour that also means “button” or “brand” or “selected nav item” would make those claims weaker. Navigation, focus, and the active scene tick are allowed to be accent only for the tick, because that tick is a position marker the size of a hairline, and focus is an ink outline so it is never confused with a warning.

**Near to far is one rule, reused.** On the track, the departure is opaque and the destination fades. On the altitude profile, the climb is a solid mass and the cruise is an outline that thins toward the far end. On the blood lattice, the floor of the column is opaque and the top of the lit mass is softer, with a bright ring on the silhouette. The eye learns the rule on scene 01 and keeps it.

**Rank is size and opacity, inside one pigment.** Hazards that contain the airplane are large and solid. Hazards on the route but above or below the airplane are smaller and dimmer. Empty slots stay on the lattice so the row does not jump around. The giant number counts only the first kind. The picture and the number agree.

**Type has three jobs and does not trade them.** Display type is the answer. Body type is the sentence. Mono is the label, the unit, the raw METAR, the source clock. A headline set in mono would look like a readout. A label set in display would look like a second headline. The squint test fails as soon as two things both look like the answer.

**Chrome frames the instrument and stays out of the sentence.** Corner brackets, the word “Corridor,” the scene number, the tick ruler, and the bottom strip are fixed. They tell you where you are. They are 10–13px, uppercase or tabular, and muted, so they cannot be mistaken for the scene’s fact.

**The map is delayed and desaturated.** OpenStreetMap tiles are greyscaled, darkened, and contrast-stretched until the land is a dark plate. Colour on the map is reserved for the route (ink, or accent where saturation is under 90) and for hazard outlines in the same ink. A full-colour map would introduce a ninth palette — park green, water blue, road yellow — and the blood-red route would have to shout over it.

## 5. Colour

All neutrals are warm. The ink is paper, not screen white. The greys are the same paper, darkened. Cool grey and blue-grey read as a generic dark theme. Purple reads as haze. Both are out.

Tokens, as implemented:

```css
:root {
  --bg: #05060a;
  --ink: #f4f1ea;
  --body: #c9c4ba;
  --muted: #9a958c;
  --dim: #1a1b20;
  --line: rgba(244, 241, 234, 0.14);
  --line-strong: rgba(244, 241, 234, 0.42);
  --accent: #ff4a2a;
  --pass: #3dba7a;
  --caution: #e2b340;
  --fail: #ff4a2a;
}
```

`--fail` and `--accent` are the same pigment on purpose. In this product the warning and the blood are the same story: saturation under 90, oxygen required, a hazard at your altitude, IFR or LIFR, a live alert, a request that failed, a feed that is down. One red means “pay attention.” A second red for errors would split that meaning.

### Where each token is allowed

| Token | Role | Use it for | Leave it off |
|---|---|---|---|
| `--bg` `#05060a` | The face of the instrument | Page, strip, map background, map control buttons, popup and tooltip fill, the fill of route endpoints | Cards, inputs, buttons. Those stay transparent so the field stays one colour |
| `--ink` `#f4f1ea` | The reading | Headlines, giant numbers, lit non-blood dots, route line, hazard stroke when the area contains the airplane, focus ring, button label, the scene number | Long paragraphs. Those are `--body` so the answer stays brighter than the sentence |
| `--body` `#c9c4ba` | The sentence | Ledes, finding lines, hit-list names | Labels, units, raw weather. Those are quieter than a sentence |
| `--muted` `#9a958c` | The scale and the caption | Kickers, fine print, units, tick labels, chrome, raw METAR, attribution, the quiet alert line, select chevron | Anything that is the answer on that screen |
| `--dim` `#1a1b20` | Unlit dots | Lattice cells that are off. Darker than the page by a step, so the grid is visible before data arrives and still recedes once cells light | Borders, text, backgrounds of panels |
| `--line` ink at 14% | The quiet seam | Frames, inputs, station rules, list rules, strip top border, map control box, popup border | Emphasis. A strong separation uses `--line-strong` |
| `--line-strong` ink at 42% | The present seam | Corner marks, the submit button’s resting border | Body rules. Those stay at 14% so lists do not look like tables |
| `--accent` `#ff4a2a` | Blood, and the position “you are here” | The word “blood,” saturation under 90, the low-SpO2 stretch of the route, lit blood dots, the scrub’s current tick, the active scene tick on the ruler | Links, buttons, focus, checkboxes, selection, decorative highlights |
| `--pass` `#3dba7a` | A real all-clear | VFR, feed “Live,” hazard “Clear,” oxygen rule “Clear” | Success toasts, positive marketing, icons that are merely “on” |
| `--caution` `#e2b340` | A real marginal state | MVFR, feed “Degraded” | Warnings that are actually failures. IFR, LIFR, and a down feed are `--fail` |
| `--fail` `#ff4a2a` | A real bad state | IFR, LIFR, “Required,” “At altitude,” “In” a hazard, the hot alert line, the form error, feed “Down” | Decoration, the accent word in a headline. That word uses `--accent`. Same hex, different job: the headline word is blood, the status word is the finding |

Flight category map, used for the big category word and for the arrival sentence:

- VFR → `--pass`
- MVFR → `--caution`
- IFR and LIFR → `--fail`
- Anything else → `--ink`, the unmarked case

### The glow

One fixed disc, behind the page (`z-index: 0`), `pointer-events: none`.

```css
.glow {
  position: fixed;
  z-index: 0;
  right: -18vw;
  top: -24vw;
  width: 52vw;
  height: 52vw;
  background: radial-gradient(
    circle,
    rgba(255, 74, 42, 0.20) 0%,
    rgba(255, 74, 42, 0.06) 32%,
    transparent 68%
  );
  pointer-events: none;
}
```

Most of the disc sits off the top-right corner. What remains on screen is a blush, strongest in the corner and gone before it reaches the headline. It is the only place the accent exists as atmosphere. It does not sit behind the lattice, and it is not repeated at the bottom.

Opacity reference for “ink, but quieter,” used on the map and on dots:

- Hazard that contains the airplane: stroke `--ink` at full, fill `--ink` at 18%
- Hazard on the route, not at the airplane’s altitude: stroke ink at 35%, fill at 5%
- Hazard elsewhere: stroke ink at 35%, fill at 2%
- Tick marks on a scale: ink at 28% (minor) and 55% (major)
- Scrub ticks: ink at 22% (ahead), 70% (behind), accent (now)
- Ruler ticks at rest: ink at 32%
- Dot edge ring: ink at 92%, inset, 1px

### Named constants in the page script

The map cannot see CSS variables, so the two colours it paints are duplicated:

```js
const ACCENT = "#ff4a2a";
const INK = "#f4f1ea";
```

Endpoint fill is the page background, `#05060a`, so the route ends in a ring rather than a solid blob. If `--bg` ever changes, change this fill with it.

## 6. Type

Three families. Three roles. The weights loaded are the weights used, so the page does not synthesize a bold or an italic.

```html
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=Instrument+Sans:wght@500;600&family=Inter:wght@400;500&display=swap">
```

```css
--display: "Instrument Sans", "Avenir Next", "Segoe UI", sans-serif;
--sans: Inter, "Avenir Next", "Segoe UI", sans-serif;
--mono: "IBM Plex Mono", ui-monospace, "SF Mono", Menlo, monospace;
```

**Instrument Sans** is the display face. It is a grotesque with enough contrast to feel cut rather than default, and at weight 600 with tight tracking it holds a 7rem number without looking spindly. Weight 500 is loaded and the page sets display at 600. The fallbacks are system grotesques so a failed font request still has a heavy sans, not a serif.

**Inter** is the body face. It is plain on purpose. The sentence should be easy and anonymous so the display line remains the event. Body copy is weight 400. Weight 500 is loaded for any medium body emphasis; the current page does not need a bolder sentence.

**IBM Plex Mono** is the instrument face. Labels, units, raw weather, source clocks, the chrome, the form, the strip. Weight 400 for long mono (a METAR, a fine line). Weight 500 for labels that are short and need to hold their tracking (kickers, the submit button, strip values, units). The fallbacks are the system monospaces.

Italic is not loaded. `<em>` inside a headline is `font-style: normal` and `color: var(--accent)`. Emphasis is colour. The word “blood” is the same roman as the words around it, shifted into the accent, so the stress survives a missing font and does not introduce a second display style.

Base text:

```css
body {
  font-family: var(--sans);
  font-size: 17px;
  line-height: 1.5;
  font-weight: 400;
  color: var(--ink);
}
```

17px is the floor for anything that is a sentence in a component that does not set its own size. The lede steps up to 18px. Mono readouts step down, hard, so they cannot be confused with prose.

### Display

Shared by `h1` and `.giant`:

```css
font-family: var(--display);
font-weight: 600;
letter-spacing: -0.04em;
line-height: 1;
font-variant-numeric: tabular-nums;
```

Headlines (`h1`):

- Size: `clamp(4rem, 6.2vw, 5.75rem)`
- Measure: `max-width: 10ch`, `text-wrap: balance`
- Tracking: −0.04em
- Line-height: 1
- Colour: `--ink`, with the stressed word in `--accent`
- Specimens: “Your blood at altitude.” “The fields.” “Where the blood is low.”

The 10ch measure forces a stack of two or three words. A headline that fits on one long line starts to look like a banner. `text-wrap: balance` keeps the second line from being a single short word when the wrap can be even.

Giant readings (`.giant`):

- Size: `clamp(4.5rem, 8vw, 7rem)`
- Tracking: −0.045em, a hair tighter than headlines, because a number has no word spaces to absorb the tightness
- Line-height: 0.92, so the number sits optically on the lede rather than inside a loose em box
- `white-space: nowrap`, so “13,500” never breaks before its unit
- Tabular numerals, so the strip and the giants do not wobble as digits change
- Specimens: the distance in nm, the cruise altitude in ft, the low saturation in percent (this one is `.giant.accent`), the hazard count, the word “Required” or “Clear”

The unit suffix (`.unit`, and `small` inside `.giant`):

```css
font-family: var(--mono);
font-size: 13px;
font-weight: 500;
letter-spacing: 0.14em;
color: var(--muted);
margin-left: 12px;
vertical-align: 18px;
```

The unit is a caption glued to the number, not a second number. “NM”, “FT”, “%”, “HIT” sit high and small, in tracked mono, muted. 18px of vertical alignment puts them near the cap height of the giant rather than on the baseline, where they would look like a stray footnote.

Station codes and categories use the same display face at a smaller giant size: `clamp(2.4rem, 4vw, 3.4rem)`, weight 600, tracking −0.04em, line-height 1. The code is `--ink`. The category takes the status colour. They share a baseline row, pushed apart (`space-between`), so the eye reads “KIXD ……… VFR” as one instrument line.

On viewports at or below 900px, both `h1` and `.giant` become `clamp(2.7rem, 12vw, 4rem)`, and the headline’s 10ch cap is lifted so a narrow column can use the width it has.

Thin headlines are a failure. Weight 400 or 500 at this size looks timid next to a lattice. If a face cannot hold 600, pick a different face. Do not compensate with a text-shadow or a colour.

### Body

The lede (`.lede`):

```css
margin: 22px 0 0;
max-width: 34rem;
color: var(--body);
font-size: 18px;
line-height: 1.45;
```

One sentence, sometimes two short ones. 34rem is about 65 characters at 18px, enough for a spoken line and short enough that the column still has air beside a 460px frame. Specimens:

- “One route. The oxygen it leaves you. The rule that follows.”
- “The route, sampled onto a lattice. Near side solid. Far side faded.”
- “Cruise altitude. The mass is the climb. The outline is the top.”
- “Lowest estimated saturation along the flight.”
- “The forecast at the destination when you land.”
- “Hazard areas that contain the airplane, not just the route.”
- “Accent is the stretch under 90. The outlines are hazard areas.”

The lede is also the place a status word can sit inline, in its status colour, inside an otherwise `--body` sentence. “KIXD at arrival, 5 h 42 min out: **VFR**.” The colour is the category, the sentence stays body type.

Finding lines and hit names are `--body` as well, at the component’s own size (findings inherit, hit names are 14px). They are sentences broken into rows, not a table.

### Mono

The kicker (`.kicker`), used above every headline and inside every frame:

```css
margin: 0 0 18px;
font-family: var(--mono);
font-size: 11px;
font-weight: 500;
letter-spacing: 0.16em;
text-transform: uppercase;
color: var(--muted);
```

Kickers name the object in the frame (“SpO2 lattice”, “Great circle”, “Profile”, “Crew”, “Passengers”) or the scene (“Blood”, “Track”, “14 CFR 91.211”). 0.16em of tracking at 11px uppercase is the house label. Chrome uses the same tracking. Anything labeled in this style is a name, not a sentence.

Fine print (`.fine`):

```css
margin: 18px 0 0;
font-family: var(--mono);
font-size: 11px;
letter-spacing: 0.04em;
line-height: 1.55;
color: var(--muted);
max-width: 36rem;
```

Tracking drops to 0.04em because this role is allowed to be a sentence: the disclaimer, the fatigue score, the source clock (“wx 07:18Z · hazards 07:18Z · taf 07:18Z · wind 07:18Z”), the station’s “now” line, the map credits. Uppercase would make a METAR unreadable, so fine print is not transformed. Raw METAR (`.raw`) is the same idea, with `word-break: break-word` and line-height 1.5, because observation strings are long tokens.

Chrome and the strip labels are the small end of the same system:

- Chrome: 11px, weight 400 except the scene number at 500, tracking 0.16em, uppercase
- Scene number: `--ink`, tracking 0.14em, 12px of space before the scene name
- Strip labels: 10px, tracking 0.14em, uppercase, `--muted`
- Strip values: 13px, weight 500, tracking 0.04em, tabular numerals, `--ink`
- Form labels: 10px, tracking 0.14em, uppercase, `--muted`
- Form values and the error: 13px and 12px, tracking 0.04em and 0.03em, not uppercase
- Tick labels: 10px, weight 400, tracking 0.08em
- Hit-list meta: 10px, tracking 0.12em, nowrap
- Map tooltips and popups: 11px, tracking 0.06em
- Attribution: 10px

Uppercase is for names of three words or fewer. Values, errors, and observations stay in their natural case so a pilot can read “KPHX” as an ident inside a sentence of mixed case, and so “13500” is not letterspaced like a label.

## 7. Space, grid, and geometry

The desktop scene is the layout. Everything else is chrome around it.

```css
.scene {
  min-height: 100vh;
  scroll-snap-align: start;
  display: grid;
  grid-template-columns: minmax(0, 1.05fr) minmax(280px, 0.95fr);
  align-items: center;
  gap: 56px;
  padding: 104px 7vw 136px 96px;
}
```

The left column is slightly wider than the right (1.05 / 0.95) because words need a measure and the object has a max width of its own. The right column will not shrink below 280px. The 56px gap is the “text never sits on the object” rule, in space. Vertical padding clears the fixed chrome (top, about 104px) and the fixed strip (bottom, about 136px). The 96px left padding clears the tick ruler, which hangs at `left: 30px`. The right padding is `7vw`, so the frame does not kiss the viewport on a wide monitor and does not leave a void on a laptop.

Scenes are top-aligned on small screens and center-aligned on wide ones. A short scene (a headline, a sentence, a lattice) sits in the optical middle. A tall scene still starts where the eye expects, because `min-height: 100vh` gives it the viewport even when the content is shorter.

The first scene is a special grid, because it has three parts and the object should occupy the full height of both text blocks:

```css
.hero {
  grid-template-areas: "copy object" "controls object";
}
```

Copy sits at the end of its cell (toward the object’s midline). Controls sit at the start of theirs. The frame spans both rows. On a phone the areas stack: copy, object, controls.

Frames:

```css
.frame {
  width: min(460px, 100%);
  justify-self: end;
  border: 1px solid var(--line);
  border-radius: 6px;
  padding: 20px 20px 18px;
}
```

460px is the object’s measure. A lattice wider than that becomes a poster and starts to compete with the headline; a lattice much narrower looks like an icon. `justify-self: end` pins the frame to the outer edge of the right column, so the gap between words and object stays honest as the viewport grows. The station column on scene 06 uses the same idea at `min(520px, 100%)`, a little wider because two display words need to sit on one row.

Interior gaps that recur:

- 18px under a kicker, under a lede’s predecessor, above a hit list
- 22px above a lede or a findings list
- 28px above the form, and between stations
- 16px between the two ends of a frame header
- 14px between a lattice and its tick scale
- 12px inside the strip, and before a unit
- 8px under a station’s meta line, and inside the scrub’s margin
- 6px between dots, and between a form label and its field

The base unit is not a strict 8px grid. It is a small set of those gaps, reused, so a frame header and a kicker share 16–18px and a section break is 22 or 28.

Z-order, from the field up:

| Layer | z-index | Notes |
|---|---|---|
| Glow | 0 | Fixed, behind everything, no pointer events |
| Main | 1 | The scenes |
| Chrome, ruler, strip | 5 | Fixed. Chrome has no pointer events, so it never blocks a click. The ruler’s buttons do. |
| Corner marks | 6 | Fixed, no pointer events, drawn over the chrome so the brackets frame the whole face, including the header |

Corner marks sit `inset: 16px` (10px below 900px). Each is a 16×16 box with a 1px `--line-strong` border and two sides removed, so the four of them read as registration marks on a plate. They are `aria-hidden`. They are not buttons and they are not a logo.

Hairlines are 1px everywhere: borders, ticks, the ruler, the scrub, the edge ring. A 2px rule starts to look like a component. The exceptions are functional strokes on the map: the route is 2px, or 3px where saturation is under 90, and an endpoint ring is 2px, because a 1px line disappears on a tile.

## 8. Chrome

Four fixed pieces. Together they say “this is a plate you are reading,” and none of them carries the scene’s fact.

### Wordmark and scene counter

```css
.chrome {
  position: fixed;
  top: 0; left: 0; right: 0;
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  padding: 28px 40px 0 92px;
  pointer-events: none;
  font-family: var(--mono);
  font-size: 11px;
  letter-spacing: 0.16em;
  text-transform: uppercase;
  color: var(--muted);
}
```

Left: the word “Corridor”. Right: the scene number in `--ink` at weight 500, then the scene name in `--muted`. The number is `01` through `08`, from `data-scene`. The name is `data-name`. The current scene is the last one whose top is above a line 45% of the way down the viewport (`scrollY + innerHeight * 0.45`). That bias makes the counter flip as the new scene takes the middle of the glass, not when a sliver of it enters at the bottom.

Padding-left of 92px lines the word up with the scene content, which itself starts at 96px. The 4px difference is negligible; both are clearing the ruler.

### Tick ruler

A vertical list of one-pixel ticks, centered on the left edge, one per scene.

```css
.ruler-y { position: fixed; left: 30px; top: 50%; transform: translateY(-50%); gap: 11px; }
.ruler-y button { width: 14px; height: 1px; padding: 8px 0; }
.ruler-y button::before { height: 1px; width: 10px; background: rgba(244, 241, 234, 0.32); }
.ruler-y button.on::before { width: 22px; background: var(--accent); }
```

The visible mark is 10×1px. The button is wider than the mark and has 8px of vertical padding so the hit target is a finger’s worth of empty air around a hairline. The active mark grows to 22px and turns accent. That is the second allowed use of the accent as a position marker (the first is the scrub’s “now” tick). Both are 1px, so they read as the needle, not as a warning.

Clicking a tick scrolls that scene to the start with smooth behavior. Each button’s accessible name is the scene name. Below 900px the ruler is removed. The scene counter in the header still works, and the corner marks move in to 10px.

### Bottom strip

```css
.strip {
  position: fixed;
  left: 0; right: 0; bottom: 0;
  background: var(--bg);
  border-top: 1px solid var(--line);
  padding: 10px 40px 14px 92px;
}
```

Solid `--bg`, not a blur. A hairline separates it from the scene. Three rows of information, always in this order:

1. The alert line. 11px mono, `--muted`, one line, ellipsis if it overflows. It turns `--fail` when the message is an alert rather than a status. Resting copy is “Quiet”, then “Opening the watch.”, then “Watching KPHX to KIXD.” A change replaces that with the change, in the fail colour: “KIXD flight category changed from VFR to IFR”.
2. The scrub. 48 ticks in a row, 16px tall. Ticks ahead of the scroll position are 6px at ink 22%. Ticks behind are 10px at ink 70%. The current tick is 14px in the accent. This is a position scale for the whole page, and it is the same gesture as the lattice: passed marks are stronger, the needle is accent.
3. The readings, in a grid of `1.5fr` plus five equal columns, aligned to the end of the line. Labels on top, values underneath.

| Column | Label | Value | Colour rule |
|---|---|---|---|
| 1 | Feed | Live, Degraded, Down, or an em dash | pass, caution, fail |
| 2 | Alt | Altitude at this moment of the flight, with thousands separators | ink |
| 3 | SpO2 | Saturation at this moment, with a percent sign | accent when under 90, otherwise ink |
| 4 | Time | `T+000` style, minutes into the flight, three digits | ink |
| 5 | Hazard | In, or Clear | fail when the airplane is inside a hazard at its altitude, otherwise pass |
| 6 | Rule | Required, or Clear | fail or pass, from crew or passenger oxygen |

The values are sampled from the briefing profile. Scroll progress `t` (0 at the top of the document, 1 at the bottom) picks the profile sample at that fraction. Flying the page flies the route. Feed health is not part of the profile; it polls `/health` every 30 seconds. The oxygen rule is the route’s rule, constant for the flight.

Below 900px the strip’s side padding drops to 16px and the readings wrap to three columns. The alert line and the scrub stay full width.

### Focus

```css
select:focus, input:focus, button:focus-visible, .ruler-y button:focus-visible {
  outline: 1px solid var(--ink);
  outline-offset: 3px;
}
```

Focus is an ink hairline, 3px off the control. It is not the accent, because a focused field is not a warning and it is not blood. The map container’s tabindex is removed so the leaflet pane does not become a tab stop in the middle of the briefing; the page’s own controls are the keyboard path.

## 9. The lattice

This is the part to copy if nothing else is copied. Every data picture on the page, except the map and the two station names, is a field of round dots on a regular grid. One painter draws all of them.

A dot is an `<i>` inside a CSS grid. Column count is set per picture. Gap is 6px. Each cell is square (`aspect-ratio: 1`) and a circle (`border-radius: 50%`). Unlit cells are `--dim`. There is no chart axis, no gridline, and no legend swatch. The frame’s kicker says what the field is.

```css
.dots { display: grid; width: 100%; gap: 6px; }
.dots i { display: block; aspect-ratio: 1; border-radius: 50%; background: var(--dim); }
.dots i.on { background: rgba(244, 241, 234, var(--o, 1)); }
.dots i.on.blood { background: rgba(255, 74, 42, var(--o, 1)); }
.dots i.edge { box-shadow: inset 0 0 0 1px rgba(244, 241, 234, 0.92); }
.dots.line { height: 120px; align-items: center; width: min(100%, calc(var(--cols) * 46px)); }
.dots.line i { transform: scale(var(--s, 0.7)); }
```

Four visual variables, and each one means one thing:

| Variable | CSS | Meaning |
|---|---|---|
| On / off | class `on`, or the dim fill | This cell counts. Off cells keep their place so the lattice does not reflow. |
| Pigment | class `blood` | This cell is blood, or a required-oxygen block, or a sample under 90. Absent `blood`, a lit cell is ink. |
| Opacity | `--o` from 0.34 to 1 | Near versus far, or floor versus crest. 1 is solid. About 0.34 is the far or the soft end. |
| Edge | class `edge` | This lit cell touches an unlit neighbour or the rim. A 1px inset ring at ink 92% turns the cluster into a form. |
| Size | `--s` on a `.line` row, default 0.7 | Magnitude, only on a single-row picture. The row is 120px tall so a dot can grow without reflowing the page. |

A single-row picture (`.line`) sets its width to `columns × 46px`, capped at the frame, so the dots stay large enough to read as objects. A full grid stretches to the frame width and the 6px gap does the sizing.

Empty state, before a briefing arrives: the same grids are painted entirely unlit. 100 dots, a 16×16, a 24×12, a row of 24, two blocks of 16, a row of 10. The instrument is on the glass before the reading is. An em dash holds the giant number until the value exists.

### 9.1 Blood lattice — scene 01

10 by 10. One hundred dots. The number lit is the lowest estimated SpO2, rounded, clamped to 0–100. Filling starts at the bottom-left and runs left to right, then up a row. A saturation of 82 lights 82 dots: eight full rows from the floor and two dots into the next. The kicker on the right reads “82 lit”.

Opacity at visual row `r` (0 is the top row, 9 is the floor):

```
o = 0.34 + 0.66 * (r / 9)
```

The floor is opaque. The top row, if it were lit, would be 0.34. The crest of a partial fill is the softer end. Every lit dot with an unlit orthogonal neighbour, or on the rim of the 10×10, gets the edge ring. The ring is what makes 82 dots read as a block with a top, rather than as a fade to nothing.

All lit dots are `blood`. This is the picture the accent was chosen for.

### 9.2 Track lattice — scene 02

16 by 16. The great-circle profile is binned into the grid. Latitude maps to rows, top of the grid north. Longitude maps to columns. A span under 0.4 degrees is treated as 0.4 so a short hop does not explode across every cell.

For each profile sample at fraction `t` along the route (0 at departure, 1 at the destination), the cell it lands in turns on. If several samples share a cell, the earliest `t` wins, so the near end stays in charge of that dot.

```
o = 1 - t * 0.72
```

Departure is opaque ink. The destination fades to about 0.28. The edge ring traces the silhouette of the path. A tick scale under the frame labels the departure ident, two unlabeled minor ticks, and the arrival ident. The kicker on the right is “KPHX → KIXD”. The giant on the left is the distance; the lede is the pair, the duration, and one wind sentence (“16 kt of tailwind.”, or the headwind, or “Wind forecast is stale.”).

### 9.3 Altitude lattice — scene 03

24 columns by 12 rows. Column `c` samples the profile at `c / 23` and finds the crest row for that altitude against the maximum altitude on the flight (at least 1,000 ft, so a flat zero does not divide).

The left 42% of the columns are a solid mass: every cell at or below the crest is on. The right 58% are the outline only: only the crest cell is on. That is the sentence on the page, drawn: “The mass is the climb. The outline is the top.” A pressurized airplane changes the sentence (“The cabin stays below the airplane.”) and an unpressurized one says “The cabin is the airplane.” The picture is still the airplane’s altitude, because the lattice is the flight profile either way.

```
o = 1 - (c / 23) * 0.62
```

The near columns are solid. The far crest fades by up to 62%. Crest cells get the edge ring, which matters most on the outline half, where the ring is the whole drawing. The right-hand kicker is the max altitude with a thousands separator and “ft”. The giant is the cruise altitude.

### 9.4 Drop row — scene 04

One row, 12 samples across the flight. Every dot is on. Colour and size carry the reading. Size from saturation:

```
s = clamp(0.42 + 0.58 * ((spo2 - 78) / 20), 0.42, 1)
```

78% sits at the small end (0.42). 98% and above sit at full size. The dot turns `blood` when saturation is under 90. The frame kicker says “Accent under 90”, which is the whole legend, and it is a label rather than a swatch list. The giant is the minimum saturation, in the accent, with a “%” unit. The lede is the minutes under 90. Fine print is “Fatigue 37 / 100”, the heuristic, kept in the caption role so it cannot be mistaken for the medical number. The tick scale runs from “0” to the duration in minutes.

### 9.5 Rule blocks — scene 05

Two lattices side by side, labeled “Crew” and “Passengers”. Each is 16 dots in 8 columns (two rows). A party that needs supplemental oxygen is fully lit, in blood, with the perimeter cells ringed. A party that does not is the unlit dim grid. Opacity on a lit block runs from 0.40 on the top row to 1.00 on the bottom, the same floor-solid habit as the blood lattice.

```
o = 0.40 + 0.60 * (row / (rows - 1))
```

The giant word is the finding: “Required” in `--fail` if either party needs oxygen, “Clear” in `--pass` if neither does. The findings list under it is the regulation’s own sentences, one per row, separated by `--line`. If there are none: “No supplemental oxygen required under 14 CFR 91.211.” The kicker of the scene is the citation, “14 CFR 91.211”, because the rule has a name and the name is a label.

Crew and passengers are split because the regulation treats them differently, and the picture has to be able to light one block and leave the other dark. A single “oxygen” lamp would hide that.

### 9.6 Reach row — scene 07

One dot per hazard the route touches, in route order by entry time. If none touch the route, the row is eight empty dots, so the frame still has an object.

| Hazard | On | Opacity | Scale |
|---|---|---|---|
| Contains the airplane (`at_your_altitude === true`) | yes | 1 | 1 |
| On the route, outside the altitude band, or no band | yes | 0.34 | 0.62 |
| Empty placeholder | no | — | 0.45 |

These dots stay ink. The “bad” claim is size and the word beside them, not a second red inside the frame. The list under the lede names up to five areas, with a meta label: “At altitude” in `--fail`, “Clear of you”, or “No band”. The giant counts only the at-altitude set. A route that clips seven areas and enters one shows a giant 1 and one full-size dot among smaller ones. That is the clarification the scene exists to make: the corridor is the volume the airplane occupies, not the ribbon on the ground.

The right-hand kicker says “Larger = at your altitude”. One variable, stated in the label role.

### 9.7 Fields — scene 06, the lattice’s absence

The two airports are not dots. A code and a category at display size, on one baseline, is already a picture a person can read across the room, and turning a METAR into a lattice would hide the thing a pilot actually checks.

Each station is a block. The first has no rule. The next has a 1px `--line` across the top and 18px of padding above it. Between them, 28px. Inside a station, top to bottom:

- Code in display, category in display, status-coloured, pushed to opposite ends
- If this is the destination and a forecast covers the arrival: a fine line, “At arrival · 5 h 42 min · …” plus a stale mark when the forecast file is stale, and a tempo or gust note when one exists
- A fine line for the current observation: wind, visibility, ceiling, age, and “stale” when the observation is older than the feed allows
- The raw METAR, and the raw TAF group when the arrival has one, in `.raw`

The lede carries the arrival category in status colour, then the altimeter and which station it came from. Missing data uses the same type roles in plain words: “No live weather feed.”, “No current observation.”, “The forecast feed is stale.”, “No forecast covers the arrival.” Empty states are sentences in the lede or the station, not illustrations.

## 10. The map — scene 08

The map is the synthesis, so it comes last. The headline is “Where the blood is low.”, with “blood” in the accent. The lede tells you the two encodings left: “Accent is the stretch under 90. The outlines are hazard areas.” The fine line is the credit: aviationweather.gov for weather and forecasts, Open-Meteo and NOAA GFS for winds, OpenStreetMap for tiles.

The frame holds a Leaflet map at `min(64vh, 620px)`, radius 4px, background `--bg`. Tiles are the standard OSM raster, then crushed:

```css
.leaflet-tile-pane { filter: grayscale(1) brightness(0.22) contrast(1.5); }
.leaflet-container { background: var(--bg); font-family: var(--mono); }
```

Greyscale removes the map’s own palette. Brightness 0.22 sinks it into the page. Contrast 1.5 gives the coast and the rivers back as faint structure, which is enough to recognise a continent and not enough to start reading road names. Zoom is capped at 10. The face is a plate, not a navigation app.

Leaflet’s own chrome is restyled to the same plate:

- Zoom bar: 1px `--line` border, 4px radius, no shadow. Buttons 28×28, `--bg` fill, `--ink` glyphs, hairline dividers.
- Attribution: transparent, 10px mono, `--muted`
- Tooltip and popup: `--bg` fill, `--ink` text, 1px `--line` border, no shadow, mono at 11px. Tooltips are 4px radius with 4px 7px padding. Popups are 6px radius with 10px 12px of content padding, matching a frame more than a map balloon.

Route drawing:

- The profile is walked in order. A run of samples at or above 90% is an ink polyline, weight 2, opacity 0.95. A run under 90% is an accent polyline, weight 3, opacity 0.95. The run includes the point where the state flips, so the colour changes at the threshold instead of leaving a gap.
- Endpoints are circle markers, radius 5, ink stroke at weight 2, fill `#05060a`. A permanent tooltip above each one (`direction: top`, offset `[0, -4]`) carries the ident. The ring-with-a-dark-center matches the edge-ring idea: outline bright, interior the page.
- The map fits the route bounds with 36px of padding. An invisible polyline of the whole route is what gets fitted, so the fit follows the flight and not the hazard polygons, which can cover half a state.

Hazard drawing, in the same ink, ranked by whether they matter to this airplane:

| Relation | Stroke | Weight | Fill opacity |
|---|---|---|---|
| On the route and at the airplane’s altitude | `#f4f1ea` | 1.5 | 0.18 |
| On the route, not at that altitude | `rgba(244, 241, 234, 0.35)` | 1 | 0.05 |
| Not on the route | `rgba(244, 241, 234, 0.35)` | 1 | 0.02 |

Popups name the product and the hazard, the altitude band (“Surface – 18,000 ft”, or “No altitude band”), and one of “At your altitude” or “On the route, not at your altitude”. A polygon that misses the route gets the name and the band only.

If Leaflet or the tiles cannot load, the frame says “Map tiles need a network. The briefing still stands.” The other seven scenes do not depend on the map. The map is the last picture, not the page.

Redraws restore the scroll position. Fitting bounds inside Leaflet will otherwise yank the page, and the scene the person is reading would be lost. When the corridor scene is the active one, the map invalidates its size so a late layout pass does not leave a grey (or black) unfinished tile pane.

## 11. Controls

The form is part of scene 01, in the left column, under the lede’s slot and above the disclaimer. It is an instrument panel, so it borrows the label style and refuses a filled button.

```css
form {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 14px 16px;
  margin-top: 28px;
  max-width: 460px;
}
```

Fields, in order: From, To, Cruise ft, Airspeed kt, a full-width “Pressurized cabin” checkbox, a full-width submit, a full-width error line. Defaults on first load are Phoenix to New Century (KPHX to KIXD) when those idents exist, 13,500 ft, 140 kt, cabin not pressurized. The selects list idents, with “ · no wx” on a station that has no live observation. That suffix is information, set in the field’s own mono, not a badge.

Labels are 10px mono, uppercase, tracked 0.14em, `--muted`, with 6px between the name and the control.

Inputs and selects:

```css
padding: 10px 12px;
border: 1px solid var(--line);
border-radius: 6px;
background: transparent;
color: var(--ink);
font-family: var(--mono);
font-size: 13px;
letter-spacing: 0.04em;
```

Transparent fill is deliberate. A filled dark-grey input would invent a second surface. The border is the same seam as a frame. Selects use a two-part 5px chevron in `#9a958c`, drawn as borders-free background images, inset so the value does not collide with it (`padding-right: 28px`). Native appearance is stripped.

The checkbox is 14×14 with `accent-color: var(--ink)`. A checked box is ink, not accent. Pressurization is a condition of the flight, not a warning.

Submit:

```css
padding: 12px 14px;
border: 1px solid var(--line-strong);
border-radius: 6px;
background: transparent;
color: var(--ink);
font-family: var(--mono);
font-size: 12px;
font-weight: 500;
letter-spacing: 0.16em;
text-transform: uppercase;
```

The label is “Brief this route”. Hover turns the border to `--ink`. Disabled drops the whole control to 45% opacity and the default cursor. There is no filled primary button anywhere on the page. A filled accent button would spend the blood colour on an action.

The error line is 12px mono, `--fail`, with a reserved `min-height: 1.2em` so a failure does not shove the disclaimer down and then pull it back. It is an `alert` role. Copy is the server’s error, in the server’s words.

## 12. Motion

Almost none.

- `scroll-snap-type: y proximity` on the document, `scroll-snap-align: start` on each scene. Proximity settles a scene when the gesture ends near a boundary. It does not trap the scroll. Reduced motion sets the snap type to `none`.
- Scene ticks scroll with `behavior: smooth`. That is the one animated transition, and it is a native scroll, not a designed easing.
- Nothing fades in. Lattices paint in one pass when the briefing arrives. The unlit grid is already there, so the change is cells turning on, which is the reading arriving.
- The strip is the life of the page. It updates on scroll, passively. No requestAnimationFrame loop, no tween.
- The alert line swaps text when a WebSocket message arrives. The colour change to `--fail` is instant. A pulse on that line would compete with the words.
- Map fits and popup opens are Leaflet’s, restyled so they do not bring Leaflet’s shadow and white balloon with them.

If a future screen wants motion, the justified kind is a value changing because the world changed, or because the person moved through the flight. A decorative loop fails the squint test the moment it becomes a second object.

## 13. Voice

The page speaks in short declarative lines. The second person shows up once, in the first headline, because the subject of the whole briefing is the reader’s blood at altitude. After that the lines name the object.

Patterns worth keeping:

- The headline is a fragment with one stressed word, or it is absent and the giant number is the headline.
- The lede is the instruction for the picture, in one breath. It often contains the encoding: “Near side solid. Far side faded.” “The mass is the climb. The outline is the top.” “Larger = at your altitude” lives in the kicker for the same reason. The picture’s rule is written next to the picture, in the label role, so there is no legend block.
- Numbers are words when they are small and parts of a sentence (“16 kt of tailwind”, “354 min under 90%”) and giants when they are the answer.
- Idents stay idents. “KPHX”, “KIXD”, “T+042”, “07:18Z”.
- Bad news is the same sentence shape as good news. “Required” and “Clear” occupy the same giant. “In” and “Clear” occupy the same strip cell. The colour changes, the layout does not, so a change in state is visible as colour and not as a jump.
- Empty and broken states are plain sentences in the role that would have held the value. “No hazard feed.” “No live weather feed.” “Map tiles need a network. The briefing still stands.” “Alert connection closed.”
- The disclaimer and the data credits are fine print. They are complete, and they are quiet.

Words the page does not use: dashboard, insight, awesome, welcome, get started, card titles that restate the heading, tooltips on static labels.

## 14. Narrow viewports

At `max-width: 900px` the instrument stacks. It does not grow a new visual system.

- The tick ruler is removed.
- Chrome padding becomes 22px on the sides, and it no longer clears a ruler.
- Scenes become one column, gap 32px, padding `104px 22px 150px`, aligned to the start so the headline is at the top of the viewport rather than floating in a short column.
- The hero order is copy, then object, then controls. The person reads the claim, sees the lattice, then reaches the form.
- Headlines and giants step down to `clamp(2.7rem, 12vw, 4rem)`. The 10ch cap comes off the headline.
- Frames and the station column stretch to the column width.
- The strip’s readings wrap to three columns, with 8px between the wrapped rows, and 16px of side padding.
- Corner marks inset to 10px.

Dots, colours, type roles, and the strip’s job stay the same. A narrow screen that introduces a new accent, a filled button, or a hamburger over the scene counter has left the language.

## 15. What this is made of

One document, `web/index.html`. Styles are in a plain `<style>` block. Behaviour is in a plain `<script>`. Leaflet 1.9.4 and the three font families come from their CDNs. There is no component library, no utility framework, and no design-token package. The tokens are the CSS variables in `:root`. The lattice painter is `paintDots`. The pictures are `spo2Grid`, `trackGrid`, `altitudeGrid`, `dropRow`, `blockGrid`, and `hitRow`.

That constraint is part of the look. A component library’s default button, card, shadow, and radius would have to be stripped back to these hairlines anyway. Starting from the tokens in this document is faster than overriding a kit.

Copy, when you build something else in this direction, is the token block in section 5, the type rules in section 6, the scene grid in section 7, the dot painter in section 9, and the squint test. The scene list in section 1 is Corridor’s content. A different product keeps the grammar and changes the facts.

## 16. Building a new screen in this language

1. Write the one fact as a sentence. If the sentence has “and” in it, it is two screens.
2. Decide the answer the person should be able to repeat after a glance. That answer becomes either a giant number with a mono unit, or a giant word, or a headline of about ten characters with one word in the accent.
3. Build one object that is the answer again, in dots, in a single map, or in a pair of display words. Give the object a frame if it is a lattice or a map. Pin the frame to the end of the right column.
4. Put a kicker above the answer, 11px mono, uppercase, tracked 0.16em, muted. Put one lede under it, 18px, in `--body`, inside 34rem.
5. If the object encodes near-to-far, make the near side solid, fade the far side toward roughly 30% opacity, and ring the silhouette. If it encodes a magnitude on a single row, use scale. If it encodes blood or a value under the meaningful threshold, use the accent and nothing else.
6. If a colour is about to be used, name the claim. Blood, all-clear, marginal, or bad. A colour without a claim is cut. Green, gold, and red do not decorate.
7. Add the screen to the ruler, the scene counter, and the scroll. Give it a two-digit number and a one-word name.
8. Keep the corner marks, the single glow, the black field, and the strip. The strip should grow a reading only when the new fact is something a person would watch change along the flight.
9. Squint. One object, one headline, one colour.

## 17. Ways the look fails

These are the specific failures the direction was written to prevent. Meeting one of them is enough to break a screen that otherwise uses the right fonts.

- A grey or purple wash over the black. The field is `#05060a` and one corner glow. A second atmospheric colour makes the page look inexpensive.
- A second accent, or the accent used on a button, a link, a checkbox, or a focus ring. The pigment stops meaning blood.
- Green or gold used for anything that is not a real status. A green dot that means “selected” and a green word that means “VFR” cannot coexist.
- A headline under weight 600, or with positive letter-spacing, or with a line-height around 1.3. At display size that looks uncertain.
- More than one display object in a viewport. Two lattices, or a lattice and a map, is two screens.
- Text drawn on top of the lattice or the map at full contrast. The column split exists so this case does not come up.
- A chart that needs a legend of several entries. Replace it with a count, a single row of sized dots, or a giant number, and write the one encoding in the kicker.
- Dots placed by hand at random coordinates. If they are not on a grid, they are texture, and texture does not carry a number.
- Radius above 10px, a pill button, a soft shadow under a card, a glass panel with nothing behind it.
- Glow on the type, the dots, the frame, or the map.
- A filled input or a filled button in a grey that is not `--bg`. It creates a second surface, and the page is one surface.
- Pure white `#fff` for body text. The ink is `#f4f1ea` so the field feels like a lit instrument rather than a default dark mode. Pure white is harsh at 7rem and chilly next to the warm accent.
- Cool grey (`#9aa`, slate, zinc) for secondary text. The whole neutral ramp stays in the paper family: `#f4f1ea`, `#c9c4ba`, `#9a958c`, `#1a1b20`.
- Animation that plays by itself. The page moves when the person scrolls or when the feed changes.
- A hamburger, a tab bar, or a card grid of equal facts. Those put several answers on one face. The ruler and the scene counter are the navigation.

## 18. Decisions, and what each one bought

The choices below are the ones that define the page. The values in sections 5 through 14 are the implementation of this list.

**Near-black, flat, one blush in the corner.** The briefing is a plate. A gradient background, a illustrated sky, or a panel in a lighter grey would have split the page into “chrome” and “content” and made the lattices look like widgets sitting on a website. One field means the dots and the words are on the instrument, not in cards on top of it. The blush keeps the black from feeling dead, and because it is the blood colour at 20% falling to nothing, it previews the accent before the first lattice explains it.

**Orange-red as the only accent, inherited from Somnium’s blood.** Corridor’s subject is what altitude does to oxygen in the blood. A blue “aviation” accent would have been on-theme for airplanes and off-theme for the fact the page is about. Using the accent for the word “blood”, the lit saturation, the stretch of route under 90, and the low reading in the strip means a person learns the colour once. Sharing that hex with failures is acceptable because every failure this product reports is in that same family of “you are in the bad part of the flight.” It would be a mistake on a product whose errors are unrelated to its accent.

**Green and gold locked to status.** VFR / MVFR / IFR is already a colour language pilots know. The page uses it and refuses to extend it. Feed health rides the same three colours because “live / degraded / down” is the same shape as “fine / marginal / bad,” and teaching a second triad would cost the clarity the first one bought. The strip’s Clear / In and the rule’s Clear / Required ride pass and fail for the same reason.

**Warm paper ink instead of pure white.** `#f4f1ea` against `#05060a` is a high contrast, and it is a soft one. Combined with the warm greys, the page feels like a printed plate under a small lamp. Pure white on pure black is a terminal, and a terminal was the wrong metaphor once the pictures became lattices instead of log lines. Mono still carries the log-line job (METAR, clocks, idents), at a small size, in the muted paper colour.

**Three faces, hard roles.** Instrument Sans at 600 does the thing the references asked for: heavy, tight, big, confident. Inter keeps the explanation out of the way. Plex Mono at 10–13px with wide tracking is how the page says “this is a scale, a name, or a raw observation” without drawing a box around it. The unit suffix is the clearest example. “13500” is the event; “FT” is the scale marking. Set the unit in the display face and the number gains a stutter. Set the number in mono and the page becomes a log.

**The stressed word is colour, not italic.** “Your blood at altitude.” The operative word changes pigment and nothing else. Italic at that size would have been a second gesture, and the accent would have had to share the emphasis. One gesture is enough.

**One idea per viewport, in flight order.** The physiology is the reason the product exists, so blood is scene 01, not a chart buried under a map. The map is scene 08, once the person knows that accent means under 90 and that an outline means a hazard. Reordering so the map came first would spend the accent before it had a definition. The order also matches a briefing a person can retell: how low the blood goes, how far the flight is, how high, how long it stays low, what the rule requires, what the fields are doing, what the airplane actually enters, and only then where all of that sits in space.

**Lattices instead of charts.** A line chart of saturation needs axes, a threshold line, and a legend, and it still asks to be read. A hundred dots, 82 of them lit, is the number. A row of twelve dots that turn red and shrink is the flight. The encoding is coarse on purpose. Twelve samples across a six-hour flight will not show a one-minute notch, and they are not supposed to. The exact sample is in the strip, which is the precise readout. The lattice is the shape of the answer.

**Near solid, far faded, edge bright.** This is the rule that makes a lattice read as a form in under a second. Without the fade, a track is a wire of identical beads and the direction of flight is invisible. Without the edge ring, a faded crest dissolves into the background and the altitude outline, which is only one dot thick, disappears. The three pictures that share the rule (blood, track, altitude) teach it by repetition, which is why a new picture in this language should reuse it before inventing a new one.

**Mass versus outline on the altitude scene.** A filled area for the whole flight would have been a blob, and the cruise — the part that actually drives the oxygen finding — would have looked the same as the climb. Solid on the near side and outline on the far side makes the climb feel like a gain and the cruise feel like a held line. The lede says exactly that, so the drawing and the sentence confirm each other.

**Size for “at your altitude,” count for “how many of those.”** Early sketches of hazard dots used the edge ring to mean “at your altitude.” The ring already means “this is the silhouette” everywhere else, and overloading it made the reach scene’s dots look outlined at random. Size took over that meaning. Larger means it contains the airplane. The kicker says so. The giant number counts the large ones. Three channels, one fact, no conflict with the edge-ring rule.

**Text in its own column.** Overlapping type on a lattice forces a scrim, and a scrim is the glass-and-haze look this direction rejects. The two-column scene makes the separation structural. The 56px gap and the 460px frame are the amounts that hold the separation on a laptop and on a wide monitor.

**Hairline instrument marks.** Corner brackets, the ruler, the scrub, the tick scales under the track and the drop, the rules between findings and between stations. They are all 1px in the paper colour at low opacity. They give the eye a scale and a registration without becoming a layout. Remove them and the page is still correct, and also plainer than it should be: the “instrument, not a web page” claim lives in these marks as much as in the type. Add a second style of mark — a thick rule, a dotted divider, an icon — and they stop reading as one set.

**The strip as the live instrument.** A briefing that did not change as you moved would be eight posters. The strip ties them to one profile. It is also where precision lives after the lattices have been deliberately coarse. Altitude to the foot, saturation to the percent, time to the minute, and a binary in-or-clear, updating with scroll. That is the “live numbers beat decoration” rule, and it is why the page does not need an animated globe to feel awake.

**Small radii, transparent controls, ink focus.** 4–6px says the corners were considered. 16px and up would say a kit decided. Transparent fields keep the single surface. The ink focus ring keeps accessibility visible while protecting the accent. The submit control is uppercase tracked mono inside a strong hairline, the same voice as a kicker, because briefing the route is an action on the instrument, and the instrument’s actions are labels, not pills.

**The map, desaturated, last.** Putting a full OSM map on scene 01 would have made Corridor look like a flight tracker with a chart beside it. Crushing the tiles and holding the map until the encodings are already known makes the geography serve the blood and the hazards. Ink for the route, accent for the low stretch, three opacities of ink for hazards. No new colour is introduced on the most colourful data source in the product.

**No glass.** The references included glass panels. They were declined because a blur needs something worth seeing through, and this page’s background is flat by design. The strip is the place glass was most tempting, and it is solid black with a hairline instead. The type scrolling under it disappears behind the plate, the way a reading disappears behind an instrument bezel, which is the metaphor we actually wanted.

**Plain sentences for empty states.** An empty illustration, a sad icon, or a skeleton of grey cards would have been a second visual language for the moments when data is missing. The unlit lattice already covers “no reading yet.” Words cover “no feed.” The type roles do not change.

## 19. Replication checklist

Use this when the goal is “make it look like Corridor,” whether the content is this briefing or another single-subject instrument.

**Field**

- [ ] Background `#05060a` on the page and on any overscroll
- [ ] One glow, top-right, accent at 20% / 6% / 0, mostly off-canvas
- [ ] Four 16px corner brackets, 1px at ink 42%, inset 16px
- [ ] No shadow, no noise, no second background, no blur

**Type**

- [ ] Instrument Sans 600 for the answer, tracking −0.04em (giants −0.045em), line-height about 1
- [ ] Headlines around `clamp(4rem, 6.2vw, 5.75rem)`, measure near 10ch, one word in `#ff4a2a` if a word carries the subject
- [ ] Giants around `clamp(4.5rem, 8vw, 7rem)`, tabular numerals, unit in 13px Plex Mono, tracked 0.14em, muted, raised off the baseline
- [ ] Inter 18px `#c9c4ba` for the one sentence, inside about 34rem
- [ ] IBM Plex Mono 10–11px, uppercase, tracking 0.14–0.16em, `#9a958c`, for every name and label
- [ ] The same mono, not uppercase, at 11–13px, for values, raw strings, and fine sentences

**Colour discipline**

- [ ] Ink, body, muted, and dim are the warm paper ramp, not slate and not pure white
- [ ] `#ff4a2a` appears only as the subject colour, a bad status, or a 1px position needle
- [ ] `#3dba7a` and `#e2b340` appear only as real all-clear and real marginal status
- [ ] Focus is a 1px ink outline, 3px outside the control

**Layout**

- [ ] Full-viewport scenes, words left, object right, about 56px between them
- [ ] Object frame `min(460px, 100%)`, 1px border at ink 14%, 6px radius, padded about 20px, pinned to the end of the column
- [ ] Fixed header with the name and `NN Name`, fixed bottom strip of live values, left-edge tick ruler
- [ ] Padding that clears the chrome: on the order of 104px top, 136px bottom, 96px left

**Object**

- [ ] Dots on a grid, 6px gap, circles, unlit `#1a1b20`
- [ ] Lit ink or lit accent, opacity for near-to-far, inset 1px ring on the silhouette
- [ ] A single row uses scale for magnitude and is about 120px tall
- [ ] The encoding is written in the kicker, in one line
- [ ] A map, if there is one, is greyscale, darkened to about 22% brightness, contrast stretched, and uses only ink plus the accent

**Behaviour**

- [ ] The precise, changing number is in the strip, sampled from position
- [ ] No self-playing animation
- [ ] Scroll snap is proximity, and reduced motion turns it off
- [ ] Empty data leaves the lattice unlit, or replaces the value with a short sentence in the same type role

**The test**

- [ ] Squint. One object, one headline, one colour.
