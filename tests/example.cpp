#include <cstdint>
#include <filesystem>
#include <string>

#include <doctest/doctest.h>

import louds;

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
