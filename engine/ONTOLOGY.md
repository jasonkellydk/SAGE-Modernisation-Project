# Engine ontology

The vocabulary of the engine and the Zero Hour port: what each term means,
where each thing lives, and which words and shapes are not used. Every new
module or folder must fit a term here (or extend this document in the same
change). The port's contract, `games/generalszh/migration/README.md`, builds on
this.

## Processes and roles

| Term | Meaning |
|---|---|
| **Peer** | A game process taking part in a match (player or observer). Every peer runs the whole simulation and its own presentation. Single player is a match with one peer. |
| **Relay** | The lockstep sequencer (`engine/net`, `LockstepServer`): collects each player's commands per tick, orders them, sends every peer the same bundle, votes on state hashes, forwards checkpoints for repair, rejoin and late observers. It never simulates and owns no ECS world. In-process for single player, the host's process on LAN. |
| **Client / server** | Networking roles only: a peer's connection to the relay, and the relay. Never used for rendering, audio or other presentation code. |
| **Host** | An executable (`games/<game>/hosts/<host>`): window, loop, wiring. No game or presentation logic. |

## Worlds and time

| Term | Meaning |
|---|---|
| **World** | One ECS world per peer. Simulation and presentation work on the same entities; there are no mirror entities or second worlds for presentation. |
| **Simulation** | The deterministic part: archetype (table) components, fixed-point math, fixed ticks (30 per second of game time; game speed changes ticks per second), hashed and checkpointed. |
| **Presentation** | What the player sees and hears of the world: rendering, audio, effects, animation, camera, UI. Runs every frame on real delta time, floats allowed. Reads the simulation, never writes it. |
| **Side-table component** | A presentation component stored outside the archetypes (sparse-set storage, like flecs `DontFragment`): adding or removing it never moves an entity, never changes chunk layout, never enters the state hash or checkpoints; it goes when the entity goes. *(Decided 2026-09-25; to be implemented in `engine/ecs`.)* |
| **Published snapshot** | What the simulation hands presentation each tick: double-buffered previous and current tick values, so presentation can interpolate and, if frames are ever pipelined, never races the next update. The first form is the spatial snapshot (`VisibleObjects`). |
| **Job system** | The one pool of workers (one per core) every part of the process runs its parallel work on; ECS systems execute as jobs on it. Features do not start their own threads or pools. |
| **Tick / frame** | A tick is one simulation step; a frame is one rendered presentation update. |
| **Interpolation** | Placing things between the previous and current tick by how far the frame is past the tick. |

## Code building blocks

| Term | Meaning |
|---|---|
| **Domain** | One real concept (health, movement, audio, rendering). Its folder holds only `components/`, `systems/`, `definitions/`, `algorithms/`, `resources/`. |
| **Component** | Plain per-entity data. Simulation components are archetype (table) storage; presentation components are side tables. |
| **Resource** | Shared per-world data, owned by the world (`ecs::ResourceStore`, one value per type; `World::EmplaceResource`): a grid, a catalog, settings, the random seed, a tick's event stream. The composition root inserts it; systems reach it through `SystemContext::Read<T>` / `Write<T>`, checked against their declared `Resources`. Each resource is its own type (never a shared alias). |
| **Definition** | Immutable configuration bound from content (legacy templates / module data), injected. |
| **Algorithm** | A pure function shared by systems or by the composition root between ticks. |
| **System** | Stateless logic the scheduler runs over a query: parallel chunk systems, or batch systems that run once on the caller (and may walk their own query). A system holds **no data** (enforced: `std::is_empty_v`): per-entity data is in components, shared data in world resources, configuration in resources, rule constants are `static constexpr` code. |
| **System group** | A named umbrella of systems and nested groups (`ecs::SystemGroupTraits`, `Parent`, group Before/After, `SystemRegistry::RegisterGroup`). Ordering between groups is soft, so a whole umbrella (e.g. Rendering with Models, Particles, Terrain, Water, Shadows) can be left out or rewritten. Engine systems never name a game's groups: the game places them at registration. |
| **Command** | A typed player, AI or script action on the deterministic command bus; the command log is the replay. |
| **Event stream** | Deterministic per-tick simulation output for presentation (shots fired, impacts, casualties, script presentation commands). |
| **Checkpoint** | A peer's full simulation state between ticks (`World` storage + resources + scripts): saves, desync repair, rejoin, late observers. |
| **State hash** | Hash of everything a checkpoint holds; peers vote on it. |
| **Composition root** | The game code that assembles a world: which systems and groups run, configured how, with which resources and definitions (`games/<game>/session` for the simulation, `games/<game>/presentation` for presentation). Cross-domain wiring always lives here. |
| **Adapter** | Binds an engine concept to an external API or format: `config/adapters/<format>`, `level/adapters/<format>`, `audio/adapters/<api>` (output backends: XAudio2, SDL3). |
| **Decoder** | Turns a file format into engine data: `audio/decoders/<library>` (FFmpeg). Kept apart from output adapters. |
| **Rendering** | Presentation that draws through `engine/graphics` renderers. Units that bind presentation data to a renderer are named `*_rendering` (never "scene"). |
| **Render phases** | Per frame, in order: **Extract** (the short sync point: copy what a feature needs from the published snapshot and render objects into its frame packet), **Visibility** (per view), **Prepare** (frame packet -> GPU-ready submit packet, in parallel), **Submit** (record draw commands). As in Destiny (GDC 2015). Phases are system groups under Rendering. |
| **Render feature** | One kind of thing drawn (models, particles, terrain, water, shadows, ...): a bundle of per-phase systems plus its frame packet, registered by the game. Leaving a feature out removes it. |
| **Frame packet** | A feature's extracted per-frame, per-view and per-visible-object data, stored as structure of arrays (one array per field); the only thing prepare and submit read, which makes rendering pipelinable. |
| **Structure of arrays** | The data layout everywhere: archetype chunks, side tables (one packed column per component), frame packets, particles, voices. No arrays of fat structs, no per-entity heap objects. |
| **Render object** | Persistent per-entity render state (look, animation clock, emitter handle): a side-table component. |
| **View** | Something rendered from: the camera, a shadow cascade, a reflection. Visibility runs per view. |

## Layers and folder purposes

Generic, cross-title building blocks live in `engine/`, each in its domain,
made to fit a game by injection and configuration. Anything that ties domains
together is composition and belongs to the game.

```
engine/
  core/             foundations with no engine dependencies: math (float + fixed), serialization
  ecs/ jobs/ time/  world storage, scheduler and system groups, job system, fixed-step time
  events/           deterministic event batches
  net/              lockstep: relay, peer protocol, checkpoints/repair, input delay, local match
  config/           format adapters -> document tree -> schema binding
  filesystem/ compression/ localization/ level/   content access and formats
  gameplay/         simulation domains: common/<domain>, rts/<domain>, fps/<domain>
  scripting/        generic trigger-script runtime (games supply vocabularies)
  camera/           camera models
  graphics/         renderers (RHI, props, terrain, water, particles, shadows, ...) and their generic systems
  audio/            mixer, sound player, output adapters, decoders
  effects/          particle simulation (generic, from injected definitions)
  gui/              MVVM building blocks and WND binding
games/generalszh/
  content/          Zero Hour content bindings (INI -> definitions)
  gameplay/<domain> Zero Hour simulation rules
  session/          simulation composition root
  presentation/     Zero Hour presentation: its content (sounds, FX lists, model states), its systems per domain
                    (rendering, audio, effects, interaction, hud: the interface's pixel geometry, camera: what scripts do
                    to the view besides moving it: shakers, the motion blur's timing), and the presentation composition root
  scripting/        Zero Hour script vocabularies
  commands/         Zero Hour command types on the command bus
  shell/            front end (MVVM)
  hud/              in-game interface (control bar, later radar and messages): headless view models over the session's view (MVVM)
  hosts/<host>/     executables
  migration/        contract + ledger
```

## Not used

- "client" for presentation code; "scene" for rendering adapters.
- Mirror entities, duplicate presentation worlds, or game-side "sync" code.
- Director, manager or god classes owning logic in hosts or presentation: logic is systems.
- Vague modules without a purpose here (`engine/presentation` was rejected for this).
- `engine/` importing `games/`; engine systems naming a game's system groups.
- Singletons, `The*` globals, legacy vocabulary types.

## Decision log

| Date | Decision |
|---|---|
| 2026-09-25 | Deterministic lockstep through a relay that never simulates; checkpoints held by peers; majority hash vote; repair, rejoin and late observers from checkpoints plus the command log. |
| 2026-09-25 | Serialization is a foundation: `engine/core/serialization`. |
| 2026-09-25 | Audio: output backends are adapters (XAudio2 preferred on Windows, SDL3 elsewhere); file formats are decoders (FFmpeg). |
| 2026-09-25 | ECS is the glue for presentation too; systems are organised in nested system groups. |
| 2026-09-25 | Composition belongs to the game; `engine/` holds only generic domain building blocks configured by injection. |
| 2026-09-25 | One world: presentation state in side-table components plus a published double-buffered snapshot; no mirror entities. |
| 2026-09-25 | "Presentation" is the name for what the player sees and hears; "client/server" are networking roles only. |
| 2026-09-25 | New systems start from research of proven designs (EnTT, Bevy, flecs, Unity DOTS, industry talks), then a design, then code. |
| 2026-09-25 | Rendering follows Destiny's architecture: extract -> visibility -> prepare -> submit, frame packets, render features as pluggable bundles, all jobs, pipelinable. |
| 2026-09-25 | Rendering is modernised with 2024-2026 practice: retained render objects with change detection, persistent GPU instance buffers and bindless materials, GPU-driven visibility (frustum + two-phase Hi-Z occlusion) with multi-draw indirect, parallel command recording in the render graph; meshlets/visibility buffer later if needed; no D3D12 Work Graphs. |
| 2026-09-25 | Multicore through one shared job system per process (simulation, presentation, effects, asset work); no dedicated simulation thread; dedicated threads only where the OS owns them (audio callback, blocking IO); frame pipelining only when profiling shows rendering bound, with the published snapshot as its sync point. |
| 2026-09-25 | Death is a state, not an event handler: the dead get a `Dying` cleanup-state component (out of the fight via command removals) and slow deaths play out in a parallel system; removal is a separate per-chunk output (`Removals`). Effects of dying are deterministic death events carried out by the game (FX by presentation; object creation lists and weapons by simulation). Bodies nothing removes linger, as the original. |
| 2026-09-25 | Systems hold no data. The world owns every resource (settings and seeds included); systems fetch them through their context, checked against their declared access; `SystemRegistry::Register` rejects non-empty system types at compile time. Data-oriented: data in components and resources, systems are transforms over it. |
| 2026-09-25 | ECS is the only architecture, presentation included (Overwatch model). Presentation state is side-table components on the simulation's own entities, registered through `SessionOptions::presentationComponents`; stateless presentation systems run in two schedules over that world (once a tick: samples; once a frame on game time: looks, treads, tires, emitters, the frame's object outputs); per-definition presentation data lives in resources filled between ticks. Hosts see the game only through `SessionView`. The `gameplay_architecture` test bans directors, managers and entity-keyed maps in presentation and hosts; the remaining debt is listed in `presentation_debt.cmake`, and that list may only shrink. |
| 2026-09-26 | The in-game interface is `games/generalszh/hud/`: MVVM like the shell; its state is read each frame from `SessionView` by free functions (never kept), its views bind WND layouts, and its orders go onto the command bus. Hosts draw and feed it. |
| 2026-09-27 | The interface's drawing geometry (the original's W3D control bar draws: power bar, later clocks and radar) is `games/generalszh/presentation/hud/`: free functions in pixels (floats), tested for legacy parity; `hud/` stays headless and float-free. |
| 2026-09-27 | Scripts that answer for the local player (MultiplayerScripts.scb: `<Local Player>`, MULTIPLAYER_* victory/defeat) run in a local script runtime the host owns and ticks after each simulation tick, with the local-match vocabulary (`games/generalszh/scripting/match_vocabulary`) and client commands only; they are never part of the lockstep simulation's scripts, state hash or checkpoints. A match's standing itself (`engine/gameplay/rts/match`) is simulation state. |
