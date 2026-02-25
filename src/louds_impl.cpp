module;

#include <fstream>
#include <iostream>

// Declare that this file implements the 'louds' module.
module louds; 

// Placed directly into the nested namespace
namespace louds::detail {

    bool write_pool_to_disk(const char* filepath, const void* header, size_t header_size, 
                            const void* next_free, size_t free_size, 
                            const void* nodes, size_t nodes_size) {
        
        std::ofstream out(filepath, std::ios::binary);
        if (!out) {
            std::cerr << "[LOUDS ERROR] Failed to open file for writing: " << filepath << "\n";
            return false;
        }

        out.write(reinterpret_cast<const char*>(header), header_size);
        out.write(reinterpret_cast<const char*>(next_free), free_size);
        out.write(reinterpret_cast<const char*>(nodes), nodes_size);

        return out.good();
    }

    bool read_pool_from_disk(const char* filepath, void* header, size_t header_size, 
                             void* next_free, size_t free_size, 
                             void* nodes, size_t nodes_size) {
        
        std::ifstream in(filepath, std::ios::binary);
        if (!in) {
            std::cerr << "[LOUDS ERROR] Failed to open file for reading: " << filepath << "\n";
            return false;
        }

        in.read(reinterpret_cast<char*>(header), header_size);
        if (in.gcount() != static_cast<std::streamsize>(header_size)) return false;

        in.read(reinterpret_cast<char*>(next_free), free_size);
        in.read(reinterpret_cast<char*>(nodes), nodes_size);

        return in.good();
    }

} // namespace louds::detail