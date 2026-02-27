# louds

`louds` is a small C++23 module for building game-world foundations around a fixed pool of fat structs.

The approach is inspired by Anton Mikhailov's "Avoiding Modern C++" talks:
- https://www.youtube.com/watch?v=ShSGHb65f3M
- https://www.youtube.com/watch?v=-m7lhJ_Mzdg

## Core idea

Use one dense, fixed-capacity pool as the world database:
- No owning pointers in gameplay data.
- Relationships are value references (`ThingRef`) and intrusive indices.
- Systems iterate the pool and branch by a `kind` enum.
- The whole world snapshot can be serialized/deserialized as raw bytes.

This favors determinism, simple memory behavior, and save/load robustness.

## Data model

The main type is:

```cpp
import louds;

louds::ThingPool<MyThing, 65536> world;
```

Important pieces:
- `ThingRef { index, generation }`: stable handle with stale-handle protection.
- `NilRef`: invalid sentinel (`index == 0`).
- `spawn()` / `destroy()`: allocate/free slots via an internal free list (`destroy()` recursively destroys descendants).
- `destroy_later(ref)` / `flush_destroy_later()`: defer structural mutation while iterating.
- `clear_destroy_later()` / `pending_destroy_count()`: manage deferred queue state.
- `queue_destroy_if(pred)`: bulk enqueue destruction from a predicate pass.
- `attach_child(parent, child)` / `detach(ref)`: intrusive hierarchy (index-based).
- Iteration (`for (auto item : pool)`): yields active items only.
- `for_kind(kind, fn)`: dispatch-friendly full-pool pass that skips non-matching kinds.
- `save_to_file()` / `load_from_file()`: snapshot/restore entire pool state.

`index = 0` is reserved as the nil slot.
`MAX_THINGS` must be at least `2` (`0` is nil, `1..MAX_THINGS-1` are allocatable slots).

Debug safety:
- `get(ref)` asserts in debug builds if `ref` is invalid.
- `load_from_file()` is transactional: on failure, pool state is unchanged.
- Deferred destroy queue is runtime-only and cleared by `load_from_file()`.
- Deferred destroy queue capacity is `MAX_THINGS - 1`; `destroy_later()` returns `false` on overflow.

## Payload rules (`T`)

`ThingPool<T, N>` is designed for "fat struct" payloads:
1. Pack all gameplay fields directly in `T`.
2. Do not store raw pointers to other things.
3. Store links as `ThingRef` or plain indices.
4. Include a kind/tag enum to drive full-pool system passes.
5. Keep `T` trivially copyable (required by save/load).

## Practical game-engine pattern

```cpp
import louds;
#include <cstdint>

enum class ThingKind : std::uint8_t {
    None,
    Player,
    Enemy,
    Projectile,
    Pickup
};

struct GameThing {
    ThingKind kind = ThingKind::None;
    float px = 0.0f, py = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    int health = 0;
    louds::ThingRef target = louds::NilRef;
};

louds::ThingPool<GameThing, 8192> world;
```

Spawn and link by value:

```cpp
const auto player = world.spawn();
const auto rocket = world.spawn();

world.get(player) = {
    .kind = ThingKind::Player,
    .px = 10.0f, .py = 4.0f,
    .health = 100
};

world.get(rocket) = {
    .kind = ThingKind::Projectile,
    .vx = 30.0f,
    .target = player
};
```

Run systems by scanning active slots:

```cpp
for (auto item : world) {
    auto& thing = item.data;
    switch (thing.kind) {
    case ThingKind::Player:
    case ThingKind::Enemy:
    case ThingKind::Projectile:
        thing.px += thing.vx * dt;
        thing.py += thing.vy * dt;
        break;
    default:
        break;
    }
}
```

System dispatch pattern (one system per kind, skip everything else):

```cpp
void update_projectiles(louds::ThingPool<GameThing, 8192>& world, float dt) {
    world.for_kind(ThingKind::Projectile, [&](louds::ThingRef, GameThing& thing) {
        thing.px += thing.vx * dt;
        thing.py += thing.vy * dt;
    });
}

void damage_enemies(louds::ThingPool<GameThing, 8192>& world, int damage) {
    world.for_kind(ThingKind::Enemy, [&](louds::ThingRef, GameThing& thing) {
        thing.health -= damage;
    });
}
```

Frame update can then compose many full-pool systems:

```cpp
void run_frame(louds::ThingPool<GameThing, 8192>& world, float dt) {
    update_projectiles(world, dt);
    damage_enemies(world, 5);
    world.flush_destroy_later(); // apply deferred destroys once per frame/system boundary
}
```

Snapshot the whole world:

```cpp
world.save_to_file("savegame.bin");
world.load_from_file("savegame.bin");
```

## More gameplay examples

Projectile hit pass with value refs only:

```cpp
for (auto item : world) {
    auto& thing = item.data;
    if (thing.kind != ThingKind::Projectile) continue;
    if (!world.is_valid(thing.target)) continue;

    auto& target = world.get(thing.target);
    target.health -= 25;
    world.destroy_later(item.ref); // safe while iterating
}
```

Stale handle safety after slot reuse:

```cpp
const auto enemy = world.spawn();
world.destroy(enemy);
const auto replacement = world.spawn(); // may reuse same index with new generation

// old handle is now stale and rejected
if (!world.is_valid(enemy)) {
    // reacquire a fresh handle through gameplay logic
}
```

Intrusive hierarchy for scene ownership:

```cpp
const auto player = world.spawn();
const auto weapon = world.spawn();
const auto muzzle_flash = world.spawn();

world.attach_child(player, weapon);
world.attach_child(weapon, muzzle_flash);
```

The hierarchy is index-based and pointer-free, so it can be snapshotted with the rest of
the pool.

## Build and test

This project uses `abel`:

```bash
abel build
abel test
```

To build and run the test executable in one step:

```bash
abel run
```
