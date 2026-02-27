module;

// --- Global Module Fragment ---
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <concepts>
#include <type_traits>

export module louds;

namespace louds {

    // --- Exported Core Types ---
    export using ThingIdx = uint32_t;
    export using Generation = uint32_t;

    export struct ThingRef {
        ThingIdx index = 0;
        Generation generation = 0;

        explicit operator bool() const { return index != 0; }
        bool operator==(const ThingRef& other) const {
            return index == other.index && generation == other.generation;
        }
        bool operator!=(const ThingRef& other) const { return !(*this == other); }
    };

    export const ThingRef NilRef = {0, 0};

    namespace detail {
        bool write_pool_to_disk(const char* filepath, const void* header, size_t header_size, 
                                const void* next_free, size_t free_size, 
                                const void* nodes, size_t nodes_size);

        bool read_pool_from_disk(const char* filepath, void* header, size_t header_size, 
                                 void* next_free, size_t free_size, 
                                 void* nodes, size_t nodes_size);
    }

    export template <typename T, size_t MAX_THINGS>
    class ThingPool {
        static_assert(MAX_THINGS >= 2, "ThingPool requires MAX_THINGS >= 2.");

    private:
        struct Node {
            Generation generation = 0;
            bool is_active = false;

            ThingIdx parent = 0;
            ThingIdx first_child = 0;
            ThingIdx next_sibling = 0;
            ThingIdx prev_sibling = 0;

            T data{}; 
        };

        struct SaveHeader {
            char magic[4] = {'L', 'O', 'G', 'C'};
            uint32_t version = 1;
            uint32_t max_things = MAX_THINGS;
            uint32_t node_size = sizeof(Node);
            ThingIdx first_free = 1;
        };

        Node nodes[MAX_THINGS] = {};
        ThingIdx next_free[MAX_THINGS] = {};
        ThingIdx first_free = 1;

        Node& get_node(ThingRef ref) {
            if (ref.index == 0 || ref.index >= MAX_THINGS || nodes[ref.index].generation != ref.generation) {
                return nodes[0]; 
            }
            return nodes[ref.index];
        }

        void destroy_idx_recursive(ThingIdx idx) {
            Node& node = nodes[idx];
            if (!node.is_active) return;

            const ThingIdx first_child = node.first_child;
            if (first_child != 0) {
                ThingIdx child = first_child;
                do {
                    const ThingIdx next_child = nodes[child].next_sibling;
                    destroy_idx_recursive(child);
                    child = next_child;
                } while (child != 0 && child != first_child);
            }

            const ThingRef ref{idx, node.generation};
            if (node.parent != 0) {
                detach(ref);
            }

            const Generation current_gen = node.generation;
            node = {};
            node.generation = current_gen;
            node.is_active = false;
            next_free[idx] = first_free;
            first_free = idx;
        }

    public:
        ThingPool() {
            for (ThingIdx i = 1; i < MAX_THINGS - 1; ++i) {
                next_free[i] = i + 1;
            }
            next_free[MAX_THINGS - 1] = 0;
        }

        ThingRef spawn() {
            if (first_free == 0) return NilRef; 
            ThingIdx idx = first_free;
            first_free = next_free[idx];
            Generation new_gen = nodes[idx].generation + 1;
            nodes[idx] = {}; 
            nodes[idx].generation = new_gen;
            nodes[idx].is_active = true;
            return {idx, new_gen};
        }

        void destroy(ThingRef ref) {
            if (!is_valid(ref)) return;
            destroy_idx_recursive(ref.index);
        }

        bool is_valid(ThingRef ref) const {
            return ref.index > 0 &&
                   ref.index < MAX_THINGS &&
                   nodes[ref.index].is_active &&
                   nodes[ref.index].generation == ref.generation;
        }

        T& get(ThingRef ref) {
            assert(is_valid(ref) && "ThingPool::get called with invalid ThingRef.");
            return get_node(ref).data;
        }

        void attach_child(ThingRef parent_ref, ThingRef child_ref) {
            Node& parent = get_node(parent_ref);
            Node& child = get_node(child_ref);
            if (&parent == &nodes[0] || &child == &nodes[0]) return;
            
            if (child.parent != 0) detach(child_ref);
            child.parent = parent_ref.index;

            if (parent.first_child == 0) {
                parent.first_child = child_ref.index;
                child.next_sibling = child_ref.index;
                child.prev_sibling = child_ref.index;
            } else {
                ThingIdx first_child = parent.first_child;
                ThingIdx last_child = nodes[first_child].prev_sibling;
                nodes[last_child].next_sibling = child_ref.index;
                child.prev_sibling = last_child;
                child.next_sibling = first_child;
                nodes[first_child].prev_sibling = child_ref.index;
            }
        }

        void detach(ThingRef ref) {
            Node& node = get_node(ref);
            if (&node == &nodes[0] || node.parent == 0) return;
            Node& parent = nodes[node.parent];

            if (node.next_sibling == ref.index) {
                parent.first_child = 0;
            } else {
                nodes[node.prev_sibling].next_sibling = node.next_sibling;
                nodes[node.next_sibling].prev_sibling = node.prev_sibling;
                if (parent.first_child == ref.index) parent.first_child = node.next_sibling;
            }

            node.parent = 0;
            node.next_sibling = 0;
            node.prev_sibling = 0;
        }

        // --- Iteration Subsystem ---
        struct PoolItem {
            ThingRef ref;
            T& data;
        };

        class Iterator {
            ThingPool* pool;
            ThingIdx current_idx;
            void advance_to_next_active() {
                while (current_idx < MAX_THINGS && !pool->nodes[current_idx].is_active) current_idx++;
            }
        public:
            Iterator(ThingPool* p, ThingIdx start_idx) : pool(p), current_idx(start_idx) {
                if (current_idx < MAX_THINGS && !pool->nodes[current_idx].is_active) advance_to_next_active();
            }
            bool operator!=(const Iterator& other) const { return current_idx != other.current_idx; }
            Iterator& operator++() { current_idx++; advance_to_next_active(); return *this; }
            PoolItem operator*() { return { ThingRef{current_idx, pool->nodes[current_idx].generation}, pool->nodes[current_idx].data }; }
        };

        Iterator begin() { return Iterator(this, 1); }
        Iterator end()   { return Iterator(this, MAX_THINGS); }

        template <typename Kind, typename Fn>
        void for_kind(const Kind& kind, Fn&& fn) {
            static_assert(
                requires(T& value, const Kind& query_kind) {
                    { value.kind == query_kind } -> std::convertible_to<bool>;
                },
                "ThingPool::for_kind requires payload T to have a comparable .kind field."
            );

            for (ThingIdx idx = 1; idx < MAX_THINGS; ++idx) {
                Node& node = nodes[idx];
                if (!node.is_active) continue;
                if (!(node.data.kind == kind)) continue;
                fn(ThingRef{idx, node.generation}, node.data);
            }
        }

        template <typename Kind, typename Fn>
        void for_kind(const Kind& kind, Fn&& fn) const {
            static_assert(
                requires(const T& value, const Kind& query_kind) {
                    { value.kind == query_kind } -> std::convertible_to<bool>;
                },
                "ThingPool::for_kind requires payload T to have a comparable .kind field."
            );

            for (ThingIdx idx = 1; idx < MAX_THINGS; ++idx) {
                const Node& node = nodes[idx];
                if (!node.is_active) continue;
                if (!(node.data.kind == kind)) continue;
                fn(ThingRef{idx, node.generation}, node.data);
            }
        }

        bool save_to_file(const char* filepath) const {
            static_assert(std::is_trivially_copyable_v<T>, "FATAL: Payload T must be trivially copyable!");
            SaveHeader header;
            header.first_free = first_free;
            
            return detail::write_pool_to_disk(filepath, &header, sizeof(SaveHeader), 
                                              next_free, sizeof(next_free), 
                                              nodes, sizeof(nodes));
        }

        bool load_from_file(const char* filepath) {
            static_assert(std::is_trivially_copyable_v<T>, "FATAL: Payload T must be trivially copyable!");
            SaveHeader header{};
            ThingIdx loaded_next_free[MAX_THINGS] = {};
            Node loaded_nodes[MAX_THINGS] = {};
            
            bool success = detail::read_pool_from_disk(filepath, &header, sizeof(SaveHeader), 
                                                       loaded_next_free, sizeof(loaded_next_free), 
                                                       loaded_nodes, sizeof(loaded_nodes));
            if (success) {
                if (header.magic[0] != 'L' || header.magic[1] != 'O' || 
                    header.magic[2] != 'G' || header.magic[3] != 'C') return false;
                if (header.max_things != MAX_THINGS || header.node_size != sizeof(Node)) return false;
                if (header.first_free >= MAX_THINGS) return false;

                std::copy_n(loaded_next_free, MAX_THINGS, next_free);
                std::copy_n(loaded_nodes, MAX_THINGS, nodes);
                first_free = header.first_free;
                return true;
            }
            return false;
        }
    };

} // namespace louds
