"""Freeze the old release's deletion set; never used to identify new boots."""
import csv
import sys
import uuid
from pathlib import Path


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def generate(source, destination):
    bases = {}
    with source.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    for row in rows:
        base = int(row["base_id"], 16)
        guid = uuid.UUID(row["variant_guid"])
        assert fnv(guid.bytes_le) == int(row["variant_id"], 16)
        suffix = guid.hex[-12:]
        assert bases.setdefault(base, suffix) == suffix
    assert len(rows) == len(bases) * 101 == 11716
    text = """// Generated from the released 0.2 variant manifest. Deletion only.
#pragma once
#include <cstdint>
#include <unordered_set>
namespace serverboots {
inline std::unordered_set<uint32_t> legacy_ids() {
    struct Base {uint32_t item;uint64_t tail;};
    constexpr Base bases[]={
"""
    for base, tail in sorted(bases.items()):
        text += f"        {{0x{base:08x}u,0x{tail}ULL}},\n"
    text += """    };
    std::unordered_set<uint32_t> result;
    for(const auto& base:bases)for(uint32_t bonus=0;bonus<=100;++bonus) {
        uint8_t guid[16]={};
        for(int i=0;i<4;++i)guid[i]=static_cast<uint8_t>(base.item>>(i*8));
        guid[4]=static_cast<uint8_t>(bonus);guid[6]=0x02;guid[7]=0x5d;guid[8]=0xa2;
        for(int i=0;i<6;++i)guid[10+i]=static_cast<uint8_t>(base.tail>>((5-i)*8));
        uint32_t hash=2166136261u;
        for(auto byte:guid) {hash^=byte;hash*=16777619u;}
        result.insert(hash);
    }
    return result;
}
}
"""
    destination.write_text(text, encoding="utf-8")
    print(f"Verified {len(rows)} legacy IDs; generated {destination.name}")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), Path(__file__).with_name("legacy_ids.h"))
