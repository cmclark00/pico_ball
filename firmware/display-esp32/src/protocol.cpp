#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <new>

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_byte_at(const char *hex, size_t hex_length, size_t byte_offset) {
    size_t p = byte_offset * 2;
    if (!hex || p + 1 >= hex_length) return -1;
    int hi = hex_nibble(hex[p]);
    int lo = hex_nibble(hex[p + 1]);
    return hi < 0 || lo < 0 ? -1 : (hi << 4) | lo;
}

int record_level(int generation, const char *hex, size_t hex_length) {
    // Stored Gen 1/2 records are mon struct + OT + nickname. Gen 3 stores the
    // standard 100-byte party record. These offsets are unencrypted party data.
    size_t offset;
    if (generation == 1) offset = 33;
    else if (generation == 2) offset = 31;
    else if (generation == 3) offset = 84;
    else return -1;
    return hex_byte_at(hex, hex_length, offset);
}

VaultProtocol::VaultProtocol() {
    memset(&snapshot_, 0, sizeof(snapshot_));
    reset();
}

VaultProtocol::~VaultProtocol() { reset(); }

void VaultProtocol::reset() {
    for (size_t i = 0; i < snapshot_.count; ++i) {
        delete snapshot_.entries[i];
        snapshot_.entries[i] = nullptr;
    }
    snapshot_.count = 0;
    snapshot_.declared_count = -1;
    snapshot_.receiving = false;
    snapshot_.complete = false;
}

bool VaultProtocol::consume(const char *line) {
    if (!line) return false;
    int count = 0;
    if (sscanf(line, "DUMP %d", &count) == 1) {
        reset();
        snapshot_.declared_count = count;
        snapshot_.receiving = true;
        return true;
    }
    if (!snapshot_.receiving) return false;
    if (strcmp(line, "END") == 0 || strcmp(line, "END\r") == 0) {
        snapshot_.receiving = false;
        snapshot_.complete = snapshot_.declared_count >= 0 &&
                             snapshot_.count == (size_t)snapshot_.declared_count;
        return true;
    }

    int generation = 0, species = 0, length = 0;
    int consumed = 0;
    if (sscanf(line, "MON %d %d %d %n", &generation, &species, &length, &consumed) == 3 &&
        snapshot_.count < VAULT_MAX_ENTRIES && consumed > 0) {
        const char *hex = line + consumed;
        size_t hex_length = strcspn(hex, "\r\n");
        if (length < 0 || length > (int)VAULT_MAX_RECORD_SIZE ||
            hex_length != (size_t)length * 2) return false;
        VaultEntry *entry_ptr = new (std::nothrow) VaultEntry{};
        if (!entry_ptr) return false;
        VaultEntry &entry = *entry_ptr;
        entry.slot = (int)snapshot_.count;
        entry.generation = generation;
        entry.species = species;
        entry.level = record_level(generation, hex, hex_length);
        entry.raw_length = (uint8_t)length;
        for (int i = 0; i < length; ++i) {
            int value = hex_byte_at(hex, hex_length, (size_t)i);
            if (value < 0) { delete entry_ptr; return false; }
            entry.raw[i] = (uint8_t)value;
        }
        snapshot_.entries[snapshot_.count] = entry_ptr;
        snapshot_.count++;
        return true;
    }
    return false;
}
