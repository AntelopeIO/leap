using namespace eosio;

class bitset {
public:
    bitset(size_t size) 
        : num_bits(size), data((size + 63) / 64) {}
    
   bitset(size_t size, const std::vector<uint64_t>& raw_bitset)
       : num_bits(size), data(raw_bitset) {
           check(raw_bitset.size() == (size + 63) / 64, "invalid raw bitset size");

       }

    // Set a bit to 1
    void set(size_t index) {
        check_bounds(index);
        data[index / 64] |= (1ULL << (index % 64));
    }

    // Clear a bit (set to 0)
    void clear(size_t index) {
        check_bounds(index);
        data[index / 64] &= ~(1ULL << (index % 64));
    }

    // Check if a bit is set
    bool test(size_t index) const {
        check_bounds(index);
        return (data[index / 64] & (1ULL << (index % 64))) != 0;
    }

    // Size of the bitset
    size_t size() const {
        return num_bits;
    }

private:
    size_t num_bits;
    std::vector<uint64_t> data;

    // Check if the index is within bounds
    void check_bounds(size_t index) const {
        check(index < num_bits, "bitset index out of bounds");
    }
};
