#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <array>
#include <fstream>
#include <type_traits>

#include <doctest/doctest.h>

import louds;

namespace {

enum class ThingKind : std::uint8_t {
    none = 0,
    player,
    enemy,
    projectile,
    pickup,
};

struct GameThing {
    ThingKind kind = ThingKind::none;
    float px = 0.0f;
    float py = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    std::int32_t health = 0;
    louds::ThingRef target = louds::NilRef;
};

static_assert(std::is_trivially_copyable_v<GameThing>);

void simulate_motion_step(louds::ThingPool<GameThing, 32>& pool, float dt) {
    for (auto item : pool) {
        auto& thing = item.data;
        switch (thing.kind) {
            case ThingKind::player:
            case ThingKind::enemy:
            case ThingKind::projectile:
                thing.px += thing.vx * dt;
                thing.py += thing.vy * dt;
                break;
            case ThingKind::none:
            case ThingKind::pickup:
                break;
        }
    }
}

template <size_t MAX_THINGS>
void apply_projectile_hits(louds::ThingPool<GameThing, MAX_THINGS>& world, std::int32_t damage) {
    for (auto item : world) {
        auto& thing = item.data;
        if (thing.kind != ThingKind::projectile) {
            continue;
        }

        if (world.is_valid(thing.target)) {
            auto& target = world.get(thing.target);
            target.health -= damage;
        }

        (void)world.destroy_later(item.ref);
    }
}

template <size_t MAX_THINGS>
void cleanup_dead_enemies(louds::ThingPool<GameThing, MAX_THINGS>& world) {
    (void)world.queue_destroy_if([](louds::ThingRef, const GameThing& thing) {
        return thing.kind == ThingKind::enemy && thing.health <= 0;
    });
}

} // namespace

TEST_CASE("ThingRef basics") {
    const louds::ThingRef nil = louds::NilRef;
    CHECK_FALSE(static_cast<bool>(nil));

    const louds::ThingRef some{1u, 7u};
    CHECK(static_cast<bool>(some));
    CHECK(some == louds::ThingRef{1u, 7u});
    CHECK(some != louds::ThingRef{2u, 7u});
}

TEST_CASE("spawn uses free list and returns nil when pool is full") {
    louds::ThingPool<int, 4> pool;
    const auto a = pool.spawn();
    const auto b = pool.spawn();
    const auto c = pool.spawn();
    const auto d = pool.spawn();

    CHECK(pool.is_valid(a));
    CHECK(pool.is_valid(b));
    CHECK(pool.is_valid(c));
    CHECK(d == louds::NilRef);
}

TEST_CASE("destroyed refs become invalid and reused slots bump generation") {
    louds::ThingPool<int, 4> pool;

    const auto first = pool.spawn();
    REQUIRE(pool.is_valid(first));
    pool.destroy(first);
    CHECK_FALSE(pool.is_valid(first));

    const auto reused = pool.spawn();
    REQUIRE(pool.is_valid(reused));
    CHECK(reused.index == first.index);
    CHECK(reused.generation > first.generation);
}

TEST_CASE("iterator visits active items only") {
    louds::ThingPool<std::int32_t, 8> pool;

    const auto a = pool.spawn();
    const auto b = pool.spawn();
    const auto c = pool.spawn();

    pool.get(a) = 10;
    pool.get(b) = 20;
    pool.get(c) = 30;
    pool.destroy(b);

    std::int32_t sum = 0;
    int count = 0;
    for (auto item : pool) {
        sum += item.data;
        count++;
    }

    CHECK(count == 2);
    CHECK(sum == 40);
}

TEST_CASE("attach and detach keep refs valid") {
    louds::ThingPool<int, 8> pool;
    const auto parent = pool.spawn();
    const auto child = pool.spawn();
    REQUIRE(pool.is_valid(parent));
    REQUIRE(pool.is_valid(child));

    pool.attach_child(parent, child);
    CHECK(pool.is_valid(parent));
    CHECK(pool.is_valid(child));

    pool.detach(child);
    CHECK(pool.is_valid(parent));
    CHECK(pool.is_valid(child));
}

TEST_CASE("destroy parent recursively destroys all descendants") {
    louds::ThingPool<int, 16> pool;

    const auto root = pool.spawn();
    const auto child_a = pool.spawn();
    const auto child_b = pool.spawn();
    const auto grandchild = pool.spawn();
    const auto unrelated = pool.spawn();
    REQUIRE(pool.is_valid(root));
    REQUIRE(pool.is_valid(child_a));
    REQUIRE(pool.is_valid(child_b));
    REQUIRE(pool.is_valid(grandchild));
    REQUIRE(pool.is_valid(unrelated));

    pool.attach_child(root, child_a);
    pool.attach_child(root, child_b);
    pool.attach_child(child_a, grandchild);

    pool.destroy(root);

    CHECK_FALSE(pool.is_valid(root));
    CHECK_FALSE(pool.is_valid(child_a));
    CHECK_FALSE(pool.is_valid(child_b));
    CHECK_FALSE(pool.is_valid(grandchild));

    CHECK(pool.is_valid(unrelated));
}

TEST_CASE("hierarchy stress: destroying root with many siblings destroys all") {
    louds::ThingPool<int, 64> pool;
    std::array<louds::ThingRef, 24> children{};

    const auto root = pool.spawn();
    REQUIRE(pool.is_valid(root));

    for (auto& child : children) {
        child = pool.spawn();
        REQUIRE(pool.is_valid(child));
        pool.attach_child(root, child);
    }

    pool.destroy(root);

    CHECK_FALSE(pool.is_valid(root));
    for (const auto child : children) {
        CHECK_FALSE(pool.is_valid(child));
    }
}

TEST_CASE("hierarchy stress: deep tree destroy invalidates full chain") {
    louds::ThingPool<int, 64> pool;
    std::array<louds::ThingRef, 16> chain{};

    chain[0] = pool.spawn();
    REQUIRE(pool.is_valid(chain[0]));

    for (size_t i = 1; i < chain.size(); ++i) {
        chain[i] = pool.spawn();
        REQUIRE(pool.is_valid(chain[i]));
        pool.attach_child(chain[i - 1], chain[i]);
    }

    pool.destroy(chain[0]);

    for (const auto ref : chain) {
        CHECK_FALSE(pool.is_valid(ref));
    }
}

TEST_CASE("hierarchy stress: repeated destroy order is stable") {
    louds::ThingPool<int, 64> pool;

    const auto root = pool.spawn();
    const auto child_a = pool.spawn();
    const auto child_b = pool.spawn();
    const auto grandchild = pool.spawn();
    const auto survivor = pool.spawn();
    REQUIRE(pool.is_valid(root));
    REQUIRE(pool.is_valid(child_a));
    REQUIRE(pool.is_valid(child_b));
    REQUIRE(pool.is_valid(grandchild));
    REQUIRE(pool.is_valid(survivor));

    pool.attach_child(root, child_a);
    pool.attach_child(root, child_b);
    pool.attach_child(child_a, grandchild);

    pool.destroy(child_a);
    CHECK_FALSE(pool.is_valid(child_a));
    CHECK_FALSE(pool.is_valid(grandchild));
    CHECK(pool.is_valid(root));
    CHECK(pool.is_valid(child_b));
    CHECK(pool.is_valid(survivor));

    pool.destroy(child_a);
    pool.destroy(root);
    pool.destroy(root);

    CHECK_FALSE(pool.is_valid(root));
    CHECK_FALSE(pool.is_valid(child_b));
    CHECK(pool.is_valid(survivor));
}

TEST_CASE("load failure is transactional and leaves existing pool state untouched") {
    louds::ThingPool<std::int32_t, 8> source;
    const auto src_ref = source.spawn();
    REQUIRE(source.is_valid(src_ref));
    source.get(src_ref) = 1234;

    const auto path =
        (std::filesystem::temp_directory_path() / "louds_pool_transactional_load_test.bin").string();
    REQUIRE(source.save_to_file(path.c_str()));

    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        REQUIRE(file.is_open());
        file.seekp(0);
        const char bad_magic = 'X';
        file.write(&bad_magic, 1);
        REQUIRE(file.good());
    }

    louds::ThingPool<std::int32_t, 8> target;
    const auto keep_a = target.spawn();
    const auto keep_b = target.spawn();
    REQUIRE(target.is_valid(keep_a));
    REQUIRE(target.is_valid(keep_b));
    target.get(keep_a) = 111;
    target.get(keep_b) = 222;

    CHECK_FALSE(target.load_from_file(path.c_str()));

    CHECK(target.is_valid(keep_a));
    CHECK(target.is_valid(keep_b));
    CHECK(target.get(keep_a) == 111);
    CHECK(target.get(keep_b) == 222);

    int active_count = 0;
    for (auto item : target) {
        (void)item;
        active_count++;
    }
    CHECK(active_count == 2);

    std::filesystem::remove(path);
}

TEST_CASE("destroy_later and flush destroy queued entities") {
    louds::ThingPool<int, 8> pool;
    const auto a = pool.spawn();
    const auto b = pool.spawn();
    const auto c = pool.spawn();
    REQUIRE(pool.is_valid(a));
    REQUIRE(pool.is_valid(b));
    REQUIRE(pool.is_valid(c));

    CHECK(pool.destroy_later(a));
    CHECK(pool.destroy_later(c));
    CHECK(pool.pending_destroy_count() == 2);

    const auto destroyed = pool.flush_destroy_later();
    CHECK(destroyed == 2);
    CHECK(pool.pending_destroy_count() == 0);
    CHECK_FALSE(pool.is_valid(a));
    CHECK(pool.is_valid(b));
    CHECK_FALSE(pool.is_valid(c));
}

TEST_CASE("destroy_later duplicates are harmless") {
    louds::ThingPool<int, 8> pool;
    const auto a = pool.spawn();
    REQUIRE(pool.is_valid(a));

    CHECK(pool.destroy_later(a));
    CHECK(pool.destroy_later(a));
    CHECK(pool.pending_destroy_count() == 2);

    const auto destroyed = pool.flush_destroy_later();
    CHECK(destroyed == 1);
    CHECK(pool.pending_destroy_count() == 0);
    CHECK_FALSE(pool.is_valid(a));
}

TEST_CASE("stale queued refs do not destroy replacement after slot reuse") {
    louds::ThingPool<int, 8> pool;
    const auto old_ref = pool.spawn();
    REQUIRE(pool.is_valid(old_ref));

    CHECK(pool.destroy_later(old_ref));
    pool.destroy(old_ref);
    const auto replacement = pool.spawn();
    REQUIRE(pool.is_valid(replacement));

    const auto destroyed = pool.flush_destroy_later();
    CHECK(destroyed == 0);
    CHECK(pool.is_valid(replacement));
}

TEST_CASE("queued parent destroy preserves recursive subtree semantics") {
    louds::ThingPool<int, 16> pool;
    const auto root = pool.spawn();
    const auto child = pool.spawn();
    const auto grandchild = pool.spawn();
    REQUIRE(pool.is_valid(root));
    REQUIRE(pool.is_valid(child));
    REQUIRE(pool.is_valid(grandchild));

    pool.attach_child(root, child);
    pool.attach_child(child, grandchild);

    CHECK(pool.destroy_later(root));
    const auto destroyed = pool.flush_destroy_later();

    CHECK(destroyed == 1);
    CHECK_FALSE(pool.is_valid(root));
    CHECK_FALSE(pool.is_valid(child));
    CHECK_FALSE(pool.is_valid(grandchild));
}

TEST_CASE("destroy_later overflow returns false and keeps state valid") {
    louds::ThingPool<int, 4> pool;
    const auto a = pool.spawn();
    const auto b = pool.spawn();
    const auto c = pool.spawn();
    REQUIRE(pool.is_valid(a));
    REQUIRE(pool.is_valid(b));
    REQUIRE(pool.is_valid(c));

    CHECK(pool.destroy_later(a));
    CHECK(pool.destroy_later(b));
    CHECK(pool.destroy_later(c));
    CHECK(pool.pending_destroy_count() == 3);

    CHECK_FALSE(pool.destroy_later(louds::ThingRef{1u, 1u}));
    CHECK(pool.pending_destroy_count() == 3);

    const auto destroyed = pool.flush_destroy_later();
    CHECK(destroyed == 3);
    CHECK(pool.pending_destroy_count() == 0);
}

TEST_CASE("pending destroy queue is cleared by load_from_file") {
    louds::ThingPool<std::int32_t, 8> source;
    const auto src_ref = source.spawn();
    REQUIRE(source.is_valid(src_ref));
    source.get(src_ref) = 777;

    const auto path =
        (std::filesystem::temp_directory_path() / "louds_pending_queue_clear_on_load.bin").string();
    REQUIRE(source.save_to_file(path.c_str()));

    louds::ThingPool<std::int32_t, 8> target;
    const auto queued_ref = target.spawn();
    REQUIRE(target.is_valid(queued_ref));
    target.get(queued_ref) = 111;
    CHECK(target.destroy_later(queued_ref));
    CHECK(target.pending_destroy_count() == 1);

    REQUIRE(target.load_from_file(path.c_str()));
    CHECK(target.pending_destroy_count() == 0);
    CHECK(target.flush_destroy_later() == 0);

    CHECK(target.is_valid(src_ref));
    CHECK(target.get(src_ref) == 777);

    std::filesystem::remove(path);
}

TEST_CASE("save and load round-trip preserves data and active set") {
    louds::ThingPool<std::int32_t, 8> original;
    const auto a = original.spawn();
    const auto b = original.spawn();
    original.get(a) = 111;
    original.get(b) = 222;
    original.destroy(a);

    const auto path =
        (std::filesystem::temp_directory_path() / "louds_pool_roundtrip_test.bin").string();

    REQUIRE(original.save_to_file(path.c_str()));

    louds::ThingPool<std::int32_t, 8> restored;
    REQUIRE(restored.load_from_file(path.c_str()));

    CHECK(restored.is_valid(b));
    CHECK(restored.get(b) == 222);
    CHECK_FALSE(restored.is_valid(a));

    std::filesystem::remove(path);
}

TEST_CASE("game-style fat structs use value refs instead of pointers") {
    louds::ThingPool<GameThing, 16> world;

    const auto player = world.spawn();
    const auto enemy = world.spawn();
    const auto projectile = world.spawn();

    REQUIRE(world.is_valid(player));
    REQUIRE(world.is_valid(enemy));
    REQUIRE(world.is_valid(projectile));

    world.get(player).kind = ThingKind::player;
    world.get(enemy).kind = ThingKind::enemy;

    auto& rocket = world.get(projectile);
    rocket.kind = ThingKind::projectile;
    rocket.target = enemy;
    CHECK(world.is_valid(rocket.target));

    world.destroy(enemy);

    CHECK(world.is_valid(projectile));
    CHECK_FALSE(world.is_valid(rocket.target));
}

TEST_CASE("game-system update iterates full pool and branches by kind enum") {
    louds::ThingPool<GameThing, 32> world;

    const auto player = world.spawn();
    const auto pickup = world.spawn();
    const auto projectile = world.spawn();

    REQUIRE(world.is_valid(player));
    REQUIRE(world.is_valid(pickup));
    REQUIRE(world.is_valid(projectile));

    world.get(player) = {
        .kind = ThingKind::player,
        .px = 5.0f,
        .py = 2.0f,
        .vx = 4.0f,
        .vy = -2.0f,
        .health = 100,
    };

    world.get(pickup) = {
        .kind = ThingKind::pickup,
        .px = 20.0f,
        .py = 30.0f,
        .health = 1,
    };

    world.get(projectile) = {
        .kind = ThingKind::projectile,
        .px = -10.0f,
        .py = 0.0f,
        .vx = 50.0f,
        .vy = 0.0f,
        .health = 1,
        .target = player,
    };

    simulate_motion_step(world, 0.5f);

    CHECK(world.get(player).px == doctest::Approx(7.0f));
    CHECK(world.get(player).py == doctest::Approx(1.0f));

    CHECK(world.get(projectile).px == doctest::Approx(15.0f));
    CHECK(world.get(projectile).py == doctest::Approx(0.0f));
    CHECK(world.is_valid(world.get(projectile).target));

    CHECK(world.get(pickup).px == doctest::Approx(20.0f));
    CHECK(world.get(pickup).py == doctest::Approx(30.0f));
}

TEST_CASE("save and load round-trip keeps game snapshot semantics") {
    louds::ThingPool<GameThing, 16> original;

    const auto player = original.spawn();
    const auto projectile = original.spawn();
    const auto pickup = original.spawn();
    REQUIRE(original.is_valid(player));
    REQUIRE(original.is_valid(projectile));
    REQUIRE(original.is_valid(pickup));

    original.get(player) = {
        .kind = ThingKind::player,
        .px = 100.0f,
        .py = 25.0f,
        .health = 75,
    };

    original.get(projectile) = {
        .kind = ThingKind::projectile,
        .px = 110.0f,
        .py = 25.0f,
        .vx = 80.0f,
        .target = player,
    };

    original.get(pickup) = {
        .kind = ThingKind::pickup,
        .px = 3.0f,
        .py = 4.0f,
        .health = 1,
    };

    original.destroy(player);

    const auto path =
        (std::filesystem::temp_directory_path() / "louds_game_snapshot_roundtrip_test.bin").string();
    REQUIRE(original.save_to_file(path.c_str()));

    louds::ThingPool<GameThing, 16> restored;
    REQUIRE(restored.load_from_file(path.c_str()));

    REQUIRE(restored.is_valid(projectile));
    REQUIRE(restored.is_valid(pickup));
    CHECK_FALSE(restored.is_valid(player));

    CHECK(restored.get(projectile).kind == ThingKind::projectile);
    CHECK(restored.get(projectile).target == player);
    CHECK_FALSE(restored.is_valid(restored.get(projectile).target));

    CHECK(restored.get(pickup).kind == ThingKind::pickup);
    CHECK(restored.get(pickup).px == doctest::Approx(3.0f));
    CHECK(restored.get(pickup).py == doctest::Approx(4.0f));

    std::filesystem::remove(path);
}

TEST_CASE("combat frame example applies projectile damage and cleans dead enemies") {
    louds::ThingPool<GameThing, 32> world;

    const auto enemy_a = world.spawn();
    const auto enemy_b = world.spawn();
    const auto projectile_a = world.spawn();
    const auto projectile_b = world.spawn();
    REQUIRE(world.is_valid(enemy_a));
    REQUIRE(world.is_valid(enemy_b));
    REQUIRE(world.is_valid(projectile_a));
    REQUIRE(world.is_valid(projectile_b));

    world.get(enemy_a) = {
        .kind = ThingKind::enemy,
        .health = 20,
    };
    world.get(enemy_b) = {
        .kind = ThingKind::enemy,
        .health = 60,
    };
    world.get(projectile_a) = {
        .kind = ThingKind::projectile,
        .target = enemy_a,
    };
    world.get(projectile_b) = {
        .kind = ThingKind::projectile,
        .target = enemy_b,
    };

    apply_projectile_hits(world, 25);
    cleanup_dead_enemies(world);
    const auto destroyed = world.flush_destroy_later();

    CHECK_FALSE(world.is_valid(projectile_a));
    CHECK_FALSE(world.is_valid(projectile_b));
    CHECK_FALSE(world.is_valid(enemy_a));
    REQUIRE(world.is_valid(enemy_b));
    CHECK(world.get(enemy_b).health == 35);
    CHECK(destroyed == 3);
}

TEST_CASE("stale target refs stay invalid when a slot is reused by a new enemy") {
    louds::ThingPool<GameThing, 16> world;

    const auto enemy = world.spawn();
    const auto projectile = world.spawn();
    REQUIRE(world.is_valid(enemy));
    REQUIRE(world.is_valid(projectile));

    world.get(enemy).kind = ThingKind::enemy;
    world.get(projectile).kind = ThingKind::projectile;
    world.get(projectile).target = enemy;

    world.destroy(enemy);
    CHECK_FALSE(world.is_valid(world.get(projectile).target));

    const auto replacement = world.spawn();
    REQUIRE(world.is_valid(replacement));
    world.get(replacement).kind = ThingKind::enemy;

    CHECK_FALSE(world.is_valid(world.get(projectile).target));
    CHECK(world.get(projectile).target != replacement);
}

TEST_CASE("for_kind dispatch pattern skips elements with wrong kind") {
    louds::ThingPool<GameThing, 16> world;

    const auto player = world.spawn();
    const auto enemy = world.spawn();
    const auto projectile = world.spawn();
    REQUIRE(world.is_valid(player));
    REQUIRE(world.is_valid(enemy));
    REQUIRE(world.is_valid(projectile));

    world.get(player) = {
        .kind = ThingKind::player,
        .px = 1.0f,
    };
    world.get(enemy) = {
        .kind = ThingKind::enemy,
        .health = 40,
    };
    world.get(projectile) = {
        .kind = ThingKind::projectile,
        .px = 10.0f,
        .vx = 2.0f,
    };

    int projectile_updates = 0;
    world.for_kind(ThingKind::projectile, [&](louds::ThingRef, GameThing& thing) {
        thing.px += thing.vx;
        projectile_updates++;
    });

    int enemy_updates = 0;
    world.for_kind(ThingKind::enemy, [&](louds::ThingRef, GameThing& thing) {
        thing.health -= 5;
        enemy_updates++;
    });

    CHECK(projectile_updates == 1);
    CHECK(enemy_updates == 1);

    CHECK(world.get(projectile).px == doctest::Approx(12.0f));
    CHECK(world.get(enemy).health == 35);

    CHECK(world.get(player).px == doctest::Approx(1.0f));

    int const_enemy_count = 0;
    const auto& const_world = world;
    const_world.for_kind(ThingKind::enemy, [&](louds::ThingRef, const GameThing& thing) {
        CHECK(thing.kind == ThingKind::enemy);
        const_enemy_count++;
    });
    CHECK(const_enemy_count == 1);
}
