# louds Public API

This document describes the full public API exported by:

```cpp
import louds;
```

## Namespace

All public symbols are in `namespace louds`.

## Type Aliases

### `using ThingIdx = uint32_t;`

Index type used for pool slots.

### `using Generation = uint32_t;`

Generation type used to invalidate stale handles.

## Struct `ThingRef`

```cpp
struct ThingRef {
    ThingIdx index = 0;
    Generation generation = 0;

    explicit operator bool() const;
    bool operator==(const ThingRef& other) const;
    bool operator!=(const ThingRef& other) const;
};
```

Represents a handle to a pool entry.

- `index == 0` means nil/invalid.
- `generation` protects against stale-handle reuse.
- `explicit operator bool()` is true when `index != 0`.

## Constant `NilRef`

```cpp
const ThingRef NilRef = {0, 0};
```

Sentinel nil handle.

## Template Class `ThingPool<T, MAX_THINGS>`

```cpp
template <typename T, size_t MAX_THINGS>
class ThingPool;
```

Fixed-capacity intrusive pool storing payload `T`.

### Template Requirements

- `T` should be value-only game data (no owning pointers).
- `T` must be trivially copyable for `save_to_file` / `load_from_file`.
- `T` should be default-initializable (`T data{}` is used internally).
- Compile-time requirement: `MAX_THINGS >= 2`.
- Effective active capacity is `MAX_THINGS - 1` because index `0` is reserved.

## Public Members

### `ThingPool()`

Builds the free-list for indices `[1, MAX_THINGS - 1]`.

Complexity: O(`MAX_THINGS`).

### `ThingRef spawn()`

Allocates one slot from the free-list.

- Returns a valid `ThingRef` when successful.
- Returns `NilRef` when full.
- Resets slot data to default state.
- Bumps generation for reused slots.

Complexity: O(1).

### `void destroy(ThingRef ref)`

Destroys an active entry.

- No-op if `ref` is invalid.
- Recursively destroys all descendants first.
- Detaches nodes from hierarchy during recursive teardown.
- Returns slot to free-list.
- Keeps slot generation so future `spawn()` can bump it.

Complexity: O(1).

### `bool is_valid(ThingRef ref) const`

Checks if a handle currently refers to an active slot with matching generation.

Complexity: O(1).

### `T& get(ThingRef ref)`

Returns mutable payload for `ref`.

Important behavior:
- Debug builds: asserts/traps when `ref` is invalid.
- Release builds: still returns internal slot `0` payload for invalid refs.
- Best practice: check `is_valid(ref)` before calling `get(ref)`.

Complexity: O(1).

### `void attach_child(ThingRef parent_ref, ThingRef child_ref)`

Attaches `child_ref` under `parent_ref` in intrusive hierarchy.

- No-op if either ref is invalid.
- If child already has a parent, it is detached first.
- Siblings are stored as circular doubly-linked indices.

Complexity: O(1).

### `void detach(ThingRef ref)`

Detaches node from its current parent/sibling chain.

- No-op if ref is invalid or has no parent.

Complexity: O(1).

### `Iterator begin()`
### `Iterator end()`

Range-for support over active items only.

- Iteration order is ascending slot index.
- Inactive entries are skipped.

Complexity:
- Full pass: O(`MAX_THINGS`).

### `template <typename Kind, typename Fn> void for_kind(const Kind& kind, Fn&& fn)`
### `template <typename Kind, typename Fn> void for_kind(const Kind& kind, Fn&& fn) const`

Dispatch helper: iterates active pool entries and invokes callback only when `data.kind == kind`.

Compile-time requirement:
- `T` must have a `.kind` field comparable to `Kind` via `operator==`.

Callback forms:
- Mutable overload: `fn(ThingRef, T&)`
- Const overload: `fn(ThingRef, const T&)`

Complexity:
- O(`MAX_THINGS`) scan with O(1) check per slot.

### `bool save_to_file(const char* filepath) const`

Writes complete pool snapshot to disk.

- Requires `std::is_trivially_copyable_v<T>`.
- Returns `true` on successful write.

Serialized data includes:
- File header (`magic`, version, pool shape metadata, free-list head).
- Free-list array.
- Full node array.

### `bool load_from_file(const char* filepath)`

Loads complete pool snapshot from disk.

- Requires `std::is_trivially_copyable_v<T>`.
- Returns `true` when file is read and header compatibility checks pass.

Compatibility checks:
- magic must be `"LOGC"`.
- `max_things` must match template `MAX_THINGS`.
- `node_size` must match current `sizeof(Node)`.

Note:
- Load is transactional. On failure, existing pool state is left unchanged.

## Nested Public Types

### `struct ThingPool<T, MAX_THINGS>::PoolItem`

```cpp
struct PoolItem {
    ThingRef ref;
    T& data;
};
```

Element type yielded by range iteration.

### `class ThingPool<T, MAX_THINGS>::Iterator`

Iterator type used by `begin()`/`end()` for range-for.

Exposed operations:
- `operator!=`
- pre-increment `operator++`
- dereference `operator*` -> `PoolItem`

## Minimal Usage

```cpp
import louds;

enum class Kind : uint8_t { none, enemy, projectile };

struct Thing {
    Kind kind = Kind::none;
    float x = 0.0f;
    float vx = 0.0f;
};

louds::ThingPool<Thing, 1024> world;

const auto e = world.spawn();
world.get(e) = {.kind = Kind::enemy, .x = 10.0f};

world.for_kind(Kind::enemy, [&](louds::ThingRef, Thing& t) {
    t.x += 1.0f;
});
```
