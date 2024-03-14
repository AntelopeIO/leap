using namespace eosio;

class bitset {
public:
    bitset(size_t size) 
        : num_bits(size), data(new uint64_t[(size + 63) / 64]()) {}
    
    bitset(size_t size, uint64_t* _data) 
        : num_bits(size), data(_data) {}

    ~bitset() {
        delete[] data;
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
    uint64_t* data;

    // Check if the index is within bounds
    void check_bounds(size_t index) const {
        check(index < num_bits, "bitset index out of bounds");
    }
};
