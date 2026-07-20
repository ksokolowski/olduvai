# The canonical per-frame order

Both engines execute the same gameplay frame. This is the contract the
cross-engine scenario diff enforces; any change to the order below must
keep the full scenario corpus green. Steps 1–7 live in
`systems/frame_runner.cpp::run_frame`; the post-frame sequence lives in
the shell loop (`presentation/game_app.cpp`).

## Inside `run_frame`

1. **Inputs** applied to the state (replays read key state for
   *frame + 1* — the reference's injection convention).
2. **Score popups** — countdown decrement (the y-move is step 7,
   post-render ordering).
3. **Entity update** — monster AI and per-type frame work.
   3b. **Balloon/glider scenery visibility** sync (before collisions).
4. **Fireball** spawn requests → motion/hit. **Falling stone** on
   surface platform levels.
5. **Player–entity collisions** (against the *previous* frame's player
   position — the original runs its object update before player
   physics).
   5b. **L1 ride nudge** — screen 9 drifts the balloon (+3, −3);
   screen 12 catapults the landing (+5, y=80) until x reaches 60, then
   detaches with a hit cooldown.
6. **The player slot** (one branch per frame):
   - invulnerability tick (unconditional, before the branch);
   - **transition-skip frame**: no walk/gravity, but the attack latch
     is still serviced (a press here starts the swing);
   - **cave-entrance descent** (three frames, then the teleport);
   - otherwise **flight physics** (a no-op unless riding on the flight
     screens) then **the player update**.
7. **Score popups** — post-render y move. Frame counter increments.

## The shell loop, per frame

- Frame-counter wrap (after fc=61 has been used → a 62-value cycle)
  drives the timer decrement and the food-out death.
- `run_frame(...)`.
- **6b** death halo init/tick.
- **6c/6d** L5 glider entry (screen-9/18 grabs) and the screen-12
  detach + fly-away.
- **6e** position clamp + death-by-fall — *before* exits and
  transitions, so both fire in glider mode (the player update returns
  early there).
- **7** cave exit, or in a secret room: trampoline *then* the exit
  check (a bounce can reach the exit threshold the same frame).
- **8** surface transitions — **secret entry takes priority**; the
  per-level transition handler runs only if no trap fired.
  **8a** cave-warp animation (never while inside a cave).
  **8b** level-complete intercept — the pseudo-exit screen never binds
  or renders.
- **8c** secret-room bubble scatter: while `secret_flag`, exactly one
  627-draw LCG pass per gameplay frame (3 draws × 209 iterations),
  entry frame inclusive and nothing at bind time.  Native runs it in
  the render gate (after the post-frame steps, before compose); the
  reference runs it as logic step 8c — same per-frame consumption, and
  it must NOT depend on visual flags (an enhanced animation that skips
  or alters these draws forks every later RNG-driven event, because
  the LCG state persists past the room).
- **9** screen change: per-screen state clear, store rebind, and the
  one-frame walk/gravity skip for the next frame.
- Render; trace snapshot is **post-render** (the reference captures at
  the frame top, i.e. after the previous frame's render — render-side
  mutations like the club decrement are already applied).

## History

Every entry in this file exists because its absence was a caught
divergence: the input phase, the spawn shield, the transition skip,
the frame-counter phase and wrap width, the invulnerability-tick and
cave-descent slots, the flight ordering vs collisions, the ride nudge
(twice), the skip-frame attack latch, the L5 glider steps, the
clamp/fall-death order, secret-entry priority, the warp-anim cave
gate, the level-complete intercept, and the secret-room bubble pass
(render-path generation + a bind-time double-draw forked the LCG at
the first secret entry of a recorded full clear).
