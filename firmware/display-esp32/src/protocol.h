#pragma once

#include <stddef.h>
#include <stdint.h>

// The Pico has 1,024 physical slots, but captures are keyed by valid species:
// 151 Gen 1 + 251 Gen 2 + 386 Gen 3 = 788 possible live entries.
constexpr size_t VAULT_MAX_ENTRIES = 800;
constexpr size_t VAULT_MAX_RECORD_SIZE = 120;

struct VaultEntry {
    int slot;
    int generation;
    int species;
    int level;
    uint8_t raw[VAULT_MAX_RECORD_SIZE];
    uint8_t raw_length;
};

struct VaultSnapshot {
    VaultEntry *entries[VAULT_MAX_ENTRIES];
    size_t count;
    int declared_count;
    bool receiving;
    bool complete;
};

class VaultProtocol {
public:
    VaultProtocol();
    ~VaultProtocol();
    VaultProtocol(const VaultProtocol &) = delete;
    VaultProtocol &operator=(const VaultProtocol &) = delete;
    void reset();
    bool consume(const char *line);
    const VaultSnapshot &snapshot() const { return snapshot_; }


private:
    VaultSnapshot snapshot_;
};

int record_level(int generation, const char *hex, size_t hex_length);
