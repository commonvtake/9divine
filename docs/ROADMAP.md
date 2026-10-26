# Roadmap

Good next features, roughly from smallest to largest. Each one is a self-contained project that's worth its own branch and pull request.

## Small
- [ ] **Prefab assets** – save an entity subtree as a `.prefab` file and instantiate it from scripts (the duplicate/remap code in `Scene::DuplicateEntity` already does most of the work).
- [ ] **Audio fade in/out** and a music cross-fade helper.
- [ ] **Animation events** – fire `OnAnimationEvent("Footstep")` at clip times (footstep sounds, attack hit frames).
- [ ] **Capsule collider** for characters.
- [ ] **Settings menu** in the demo (resolution, volume sliders, key rebinding using `Input::BeginRebind`).
- [ ] **Asset thumbnails** for models in the asset browser (render to a small framebuffer).

## Medium
- [ ] **Rotational rigid-body dynamics** – inertia tensors, angular velocity, contact points and an OBB-vs-OBB SAT test.
- [ ] **Spatial hash / BVH** broad phase for large scenes.
- [ ] **Cascaded shadow maps** for long view distances, and point-light shadows (cube maps).
- [ ] **Post-processing stack** – HDR target, bloom, SSAO, FXAA.
- [ ] **Terrain texture painting** with a splat map and real textures.
- [ ] **Multi-scene editing** and additive scene loading.

## Large
- [ ] **PBR renderer** (metal/roughness, image-based lighting) or a **deferred** path for many lights.
- [ ] **Polygon nav mesh** (voxelize → regions → contours → polygons, like Recast) and crowd avoidance.
- [ ] **Networking v2** – binary bit-packed snapshots, sequence numbers and acks, a reliable channel, client-side prediction with server reconciliation, interest management.
- [ ] **C# or Lua scripting** with hot reload (embed Mono/.NET or Lua; bind the same `ScriptableEntity` API).
- [ ] **Vulkan or DirectX 12 backend** behind a render-hardware-interface layer.
- [ ] **Job system** to run animation, particles and physics in parallel.
