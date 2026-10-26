# 9divine – Architecture

This document explains how each part of the engine works and why it was built that way. It's meant to be read alongside the code (`engine/src/nd/`).

## Contents
1. [Application & main loop](#1-application--main-loop)
2. [Entity Component System](#2-entity-component-system)
3. [Scenes, hierarchy and serialization](#3-scenes-hierarchy-and-serialization)
4. [Scripting](#4-scripting)
5. [Rendering](#5-rendering)
6. [Physics](#6-physics)
7. [Animation](#7-animation)
8. [Audio](#8-audio)
9. [Terrain, foliage and water](#9-terrain-foliage-and-water)
10. [Navigation and AI](#10-navigation-and-ai)
11. [Game UI](#11-game-ui)
12. [Particles](#12-particles)
13. [Input](#13-input)
14. [Assets](#14-assets)
15. [Save system](#15-save-system)
16. [Networking](#16-networking)
17. [Editor](#17-editor)
18. [Build / export](#18-build--export)
19. [Profiling and debugging](#19-profiling-and-debugging)
20. [Known limitations](#20-known-limitations)

---

## 1. Application & main loop
`core/Application.cpp`

`Application` owns the GLFW window, the OpenGL 3.3 core context (loaded with glad), Dear ImGui and the engine singletons (renderer, audio). The editor and the player are both subclasses that override `OnInit / OnUpdate / OnImGui / OnShutdown`.

Each frame:
1. Measure `dt` (clamped to 0.1 s so a hitch or a breakpoint can't explode the simulation), apply `Time::Scale()`.
2. `Input::BeginFrame()` → `glfwPollEvents()` → `Input::EndFrame()` (keyboard/mouse/gamepad snapshots, so "pressed this frame" is just `now && !before`).
3. `OnUpdate(dt)` – the app updates and renders the scene.
4. ImGui frame (`OnImGui`).
5. Audio cleanup, optional screenshot, swap buffers, profiler bookkeeping.

**Design note:** GPU objects can outlive the GL context (static caches such as primitive meshes). Destructors check `g_GLContextAlive` so shutting down never calls into a destroyed driver.

## 2. Entity Component System
`scene/ECS.h`

A **sparse set** per component type:

- `sparse[entityIndex] → denseSlot`, `dense[slot] → entity`, `data[slot] → component`.
- Add/remove/has are O(1). Removal is swap-and-pop, so iteration over `dense` is cache-friendly and has no holes.
- Components live in a `std::deque`, so **references stay valid when other components of the same type are added** (a `std::vector` would reallocate). This matters because gameplay code often holds a `TransformComponent&` while spawning entities.

**Entity handles** are 32 bits: 20-bit index + 12-bit version. Destroying an entity bumps the version of its slot, so a stale handle (`Entity` stored by a script) fails `Valid()` instead of silently pointing at a new entity that reused the slot.

`View<A, B>(fn)` picks the **smallest** pool among the requested types and checks the others for each entity – the classic sparse-set join. It iterates a snapshot of IDs so callbacks may destroy entities.

## 3. Scenes, hierarchy and serialization
`scene/Scene.cpp`, `scene/SceneSerializer.cpp`, `scene/SceneManager.cpp`

- Every entity has an `IDComponent` (64-bit random UUID), `TagComponent` (name, tag, active) and `TransformComponent`.
- **Hierarchy** is stored as a `parent` UUID on the transform. World matrices are computed by walking up the parent chain. Storing only the parent keeps the data trivially serializable and makes re-parenting a single assignment.
- `SetParent(child, parent, keepWorld)` recomputes the local transform from `inverse(parentWorld) * childWorld` so objects don't jump when re-parented, and refuses to create cycles.
- **Serialization** is JSON (nlohmann/json). Enums are written as strings and every read uses a safe getter with a default, so old scene files keep loading when components gain new fields. Terrain heights are stored as base64 floats.
- **Duplication** serializes the entity subtree, assigns fresh UUIDs, and remaps parent links *and* entity references inside script fields.
- **Destruction during gameplay is deferred** to the end of the frame, so a script can destroy itself (or something it collided with) mid-iteration safely.
- **SceneManager** performs scene switches between frames (`LoadScene` only records the request). Callbacks after the load let the save system restore a snapshot into the fresh scene.
- **Play mode** in the editor copies the scene via serialization; stopping simply drops the copy, so nothing the game does can corrupt the edited scene.

## 4. Scripting
`scene/ScriptableEntity.h`

Gameplay is written in C++. A script derives from `ScriptableEntity` and overrides lifecycle methods. `ND_SCRIPT(MyScript)` creates a static registrar that adds a factory to `ScriptRegistry`, so scenes can store scripts **by class name**.

The `game/` target is a CMake **OBJECT library**: with a static library the linker would drop translation units that nothing references, and the static registrars would never run.

**Reflection without macros:** a script's `Reflect(FieldVisitor&)` lists its fields. The same function is used by three visitors:
- `LoadVisitor` – JSON → fields (scene load),
- `SaveVisitor` – fields → JSON (scene save, checkpoints),
- `ImGuiFieldVisitor` – fields → inspector widgets (editor).

Callbacks: `OnCreate`, `OnStart` (after every script's `OnCreate`, so cross-script lookups work), `OnUpdate`, `OnFixedUpdate` (60 Hz, before physics), `OnLateUpdate` (after physics and animation – used by cameras), collision/trigger enter/exit, `OnGUI`, `OnUIButton`, `OnMessage`, `OnNetworkMessage`, `OnDestroy`.

## 5. Rendering
`renderer/Renderer.cpp`, `assets/shaders/*.glsl`

A **forward renderer** with these passes:

1. **Shadow pass** – one orthographic shadow map for the first directional light. The light frustum is centred in front of the camera and **snapped to whole texels** so shadows don't shimmer as the camera moves. Polygon offset + normal-offset sampling remove acne. Sampling uses hardware depth comparison (`sampler2DShadow`) with a 3×3 PCF kernel and fades out at the map's edge.
2. **Main pass** into an off-screen framebuffer with two colour attachments: `RGBA8` colour and `R32I` entity IDs (used for mouse picking in the editor).
   - Procedural sky (fullscreen triangle; view rays reconstructed from the inverse view-projection).
   - Opaque meshes (Blinn-Phong, up to 8 point lights sorted by distance + 4 spot lights, hemisphere ambient). Skinned meshes use a `SKINNED` shader variant.
   - Terrain (height/slope based material blend), instanced trees, instanced wind-animated grass with distance fade.
   - Water (vertex waves with analytic normals, Fresnel, sun glint), alpha-blended.
   - Particles (instanced billboards, additive or sorted alpha).
   - Debug lines.
3. **Game UI** batch on top.

Shaders are single files with `#type vertex` / `#type fragment` sections, an `#include` preprocessor and compile-time variants (`SKINNED`, `INSTANCED`) cached per define set. Colours are authored in sRGB and converted to linear in the shader; output goes through ACES tone mapping + gamma.

Non-opaque passes mask out the ID attachment with `glColorMaski` so transparent effects never break picking.

**Frustum culling** uses planes extracted from the view-projection matrix (Gribb–Hartmann) and transformed AABBs.

## 6. Physics
`physics/Physics.cpp`

An **impulse-based** solver run at a fixed 60 Hz:

1. **Gather** collider bodies (sphere, box) with world-space transforms.
2. **Integrate** dynamic bodies: gravity, accumulated forces, linear drag (semi-implicit Euler). Kinematic bodies move by their velocity and push dynamic ones but are never pushed.
3. **Broad phase** – sort-and-sweep along X, then an AABB test on Y/Z.
4. **Narrow phase** – sphere/sphere, sphere vs **oriented** box (sphere centre transformed into box space and clamped), box/box (axis-aligned overlap of world bounds; minimum-penetration axis).
5. **Resolution** – positional correction split by inverse mass (with a small slop to avoid jitter), then a normal impulse with restitution and a Coulomb friction impulse clamped by `μ·j`. Repeated for several iterations so stacks settle.
6. **Terrain** – bodies are pushed out of the heightfield and lose velocity into the surface.
7. **Events** – contact pairs are diffed against the previous step to produce enter/exit events for collisions and triggers.

Also: raycasts (sphere, slab test against OBBs, terrain march + bisection; vertical rays sample the heightfield directly), `OverlapSphere`, `grounded` flags with ground normals, and a batch-query mode used by nav baking.

## 7. Animation
`animation/Animation.cpp`

- **Import:** the glTF loader builds a skeleton from the skin's joints *plus all of their ancestors* (so armature transforms are included), ordered parents-first so global transforms are one forward pass. Vertex joint indices are remapped from skin order to skeleton order.
- **Sampling:** binary search for the keyframe pair, lerp translation/scale, **slerp** rotation (cubic-spline tracks use their value keys).
- **Skinning:** `skin[i] = global[i] * inverseBind[i]`, uploaded as a uniform array; the vertex shader blends four bone matrices.
- **Animator:** states play a clip or a **1D blend space** (e.g. idle → walk → run driven by `Speed`). Blend spaces keep clips **phase-synchronised** (normalised time) so feet don't slide during the blend. Transitions are condition lists over float/bool/trigger parameters, with optional exit time and a cross-fade duration. Triggers auto-reset when consumed.
- **Root motion:** the root joint's horizontal delta is removed from the pose and applied to the entity's transform.

## 8. Audio
`audio/Audio.cpp`

Built on **miniaudio**'s high-level engine. Each playing sound is a handle in a map; one-shots are cleaned up when they finish. Sounds go through **Music** and **SFX** groups under a master volume. Spatial sounds use inverse-distance attenuation with min/max distances; the listener follows an `AudioListener` component or the primary camera. Music is streamed from disk; short effects are fully decoded. If no audio device exists, the system disables itself and every call becomes a no-op.

## 9. Terrain, foliage and water
`terrain/Terrain.cpp`

- **Generation:** fractal value noise (fBm), reshaped with a power curve, a flattened central plateau (a guaranteed place to start) and edges that sink into the sea.
- **Mesh:** a grid with normals from central differences, rebuilt with `glBufferSubData` when sculpted.
- **Sculpting:** raise/lower/smooth/flatten with a smooth falloff brush, in terrain-local space so terrains can be moved or rotated.
- **Foliage:** deterministic scattering (seeded) filtered by height and slope. Grass clumps are a tiny procedural mesh drawn with **one instanced draw call**; per-instance data (position, height, yaw, tint) is packed into the instance matrix. Wind sway is done in the vertex shader. Trees are instanced too (built-in stylised tree or any model).
- **Water:** a dense grid displaced by summed sines in the vertex shader.

## 10. Navigation and AI
`ai/Navigation.cpp`

- **Baking:** for every cell of a grid, cast a ray down against the static world (terrain + colliders). The cell is walkable if the surface isn't too steep, isn't below the minimum height (e.g. water), and an agent-sized sphere fits there.
- **Pathfinding:** A* with an octile heuristic, 8-way moves, no corner cutting, and a step-height limit between neighbours (so the top of a wall is a surface, but not reachable). The path is **string-pulled** with Bresenham line-of-sight checks to remove zig-zags.
- **Agents:** re-path on an interval, steer along waypoints with acceleration and a turn-rate limit, and stick to the ground.
- **StateMachine:** named states with enter/update/exit lambdas and time-in-state.
- **Behaviour trees:** Sequence, Selector (re-evaluated every tick so higher priority branches interrupt), Parallel, Action, Condition, Inverter, Repeat, Wait, Cooldown.

The demo shows both: the *Shade* enemy is a state machine (Patrol → Chase → Attack with line-of-sight), the foxes are a behaviour tree (flee if the player is close, otherwise wander and rest).

## 11. Game UI
`ui/GameUI.cpp`

An **immediate-mode** API (`UI::Button(...)` returns true when clicked) drawn as one batched vertex buffer with a texture switch only when needed. Text uses an `stb_truetype` font atlas packed with oversampling; a white texel in the same atlas draws solid panels, so most UI is a single draw call. Layout uses a 1920×1080 reference canvas scaled by window height plus anchors, so HUDs work at any resolution. `UIElement` components provide retained UI that designers can edit in the inspector; buttons call `OnUIButton(entityName)` on scripts.

## 12. Particles
`particles/Particles.cpp`

CPU simulation (emission rate + bursts, shapes, gravity, drag, colour/size over lifetime, spin), uploaded each frame as instance data and drawn as camera-facing quads in one instanced draw per emitter. Alpha-blended emitters are sorted back-to-front; additive ones don't need sorting.

## 13. Input
`core/Input.cpp`

Raw state for keys, mouse buttons and gamepads (via GLFW's gamepad mappings) is snapshotted each frame. On top of that, an **InputMap** of named *actions* (buttons) and *axes* (values) lets gameplay code ask for `"Jump"` or `"MoveX"` instead of hard-coded keys. Any binding type can feed any action/axis (e.g. keys contribute ±1 to an axis, a stick contributes its value with a dead zone, the mouse contributes deltas). **Rebinding** captures the next key/button/stick movement. Maps are saved as JSON per project. The editor disables game input unless the viewport is focused.

## 14. Assets
`assets/AssetManager.cpp`

Assets are referenced by project-relative paths and cached. `PollChanges()` checks file timestamps once a second and evicts changed files; components notice the asset version bump and reload, giving **hot reload** of models and textures while the editor is open. Loaders: glTF/GLB via cgltf (embedded, external and data-URI images), OBJ via tinyobjloader, FBX via ufbx (axis/unit normalisation), images via stb_image.

## 15. Save system
`save/SaveGame.cpp`

A JSON document of key/value pairs written to numbered slots in the user's app-data folder. `CaptureScene` stores the transform, active state and reflected script fields of every `Saveable` entity; `RestoreScene` applies them to a freshly loaded scene and **destroys saveable entities that no longer existed at save time** (so collected items stay collected). Checkpoints are a dedicated slot; restoring a save switches scene first, then applies the snapshot in the scene-loaded callback.

## 16. Networking
`net/Socket.cpp`, `net/Network.cpp`

Non-blocking UDP (Winsock/BSD sockets) with a simple client/server protocol:

- **Handshake:** the client sends `Hello` until it gets `Welcome` with its peer ID and the peer list. The server tells existing clients about the new peer.
- **Liveness:** pings every second; peers that are silent for 6 s time out. `Bye` on clean disconnect.
- **Replication:** at 20 Hz each peer sends a snapshot of the entities it owns (position + rotation). The server applies client snapshots and relays them; receivers **interpolate** towards the latest state.
- **Players:** each peer gets a copy of the scene's player prefab. The local copy keeps its camera and input; remote copies become kinematic puppets.
- **Messages:** `Network::Send(channel, json)` reaches every peer's `OnNetworkMessage`.

Payloads are JSON for readability and debuggability. The roadmap lists the production-grade next steps (binary packing, sequence numbers, reliability layer, prediction).

## 17. Editor
`editor/src/`

- **Project Hub** (`ProjectHub.cpp`): the start screen. Templates live in `game/src/ProjectTemplates.cpp` and are built with the same `Scene` + `SceneSerializer` API games use, so a template is just code that creates entities. Recent projects are stored in the user's app-data folder. Switching projects stops play mode, clears the asset cache, reloads the input map and opens the start scene; requests go through `RequestProjectChange`, which shows the unsaved-changes prompt and runs the switch at the start of the next frame (never in the middle of drawing UI).
- Docking layout with Viewport, Hierarchy, Inspector, Assets, Console, Profiler, Scene Settings, Input Map, Multiplayer and Project Settings panels.
- **Picking** reads the entity ID attachment under the mouse; clicking a model part selects its root first, then the part.
- **Gizmos** with ImGuizmo; the manipulated world matrix is converted back to the parent's local space.
- **Undo/redo** stores whole-scene snapshots, but only when an edit is *committed* (widget released, gizmo released, entity created/deleted). That keeps every inspector widget undoable with zero per-widget code.
- **Inspector** with drag & drop of assets onto fields and entities onto references, animator state/transition editing, and terrain sculpting tools.
- **Play / Pause / Step**, game view vs. scene view during play.

## 18. Build / export
`editor/src/ProjectBuilder.cpp`

Copies the player executable (renamed after the project), the engine's shaders and fonts, and the project folder, then writes `game.json` so the player knows where the game lives. The player looks for `EngineAssets/` next to itself before falling back to the source tree, so the same binary works in development and in a shipped build.

## 19. Profiling and debugging
`core/Profiler.cpp`, `renderer/DebugDraw.cpp`

`ND_PROFILE_SCOPE("Name")` records RAII timings into per-system averages; the renderer counts draw calls and triangles; process memory is read from the OS. `DebugDraw` collects lines (boxes, spheres, paths, grids) from anywhere during the frame and draws them in one call. Collider and nav-grid overlays are toggles in the editor.

## 20. Known limitations
Honest list of simplifications – each is a good next project (see [ROADMAP.md](ROADMAP.md)):
- Rigid bodies don't rotate from collisions (no angular dynamics); box-vs-box uses world-aligned bounds.
- Navigation is grid based rather than a polygon nav mesh.
- One shadow-casting light; no PBR or post-processing stack.
- Networking is unreliable-channel snapshots without prediction or lag compensation.
- FBX import is static meshes only (skinned FBX → convert to glTF).
- Scripting is C++ only (no hot reload of game code).
