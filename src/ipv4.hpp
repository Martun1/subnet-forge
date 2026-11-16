// ipv4.hpp: small, header-only IPv4 / subnetting library used by subnet-forge.
// Author: Martin Ivanov (Martun1) | License: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf {

inline uint32_t parse_ip(const std::string& s) {
    uint32_t out = 0;
    int parts = 0;
    std::istringstream is(s);
    std::string tok;
    while (std::getline(is, tok, '.')) {
        if (tok.empty() || tok.size() > 3 || !std::all_of(tok.begin(), tok.end(), ::isdigit))
            throw std::invalid_argument("invalid IPv4 address: " + s);
        int v = std::stoi(tok);
        if (v > 255) throw std::invalid_argument("octet out of range in: " + s);
        out = (out << 8) | static_cast<uint32_t>(v);
        ++parts;
    }
    if (parts != 4) throw std::invalid_argument("invalid IPv4 address: " + s);
    return out;
}

inline std::string to_string(uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." +
           std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
}

inline std::string to_binary(uint32_t ip) {
    std::string s;
    for (int i = 31; i >= 0; --i) {
        s += ((ip >> i) & 1) ? '1' : '0';
        if (i % 8 == 0 && i) s += '.';
    }
    return s;
}

inline uint32_t prefix_to_mask(int p) { return p == 0 ? 0 : 0xFFFFFFFFu << (32 - p); }

inline int mask_to_prefix(uint32_t mask) {
    int p = 0;
    while (p < 32 && (mask & (0x80000000u >> p))) ++p;
    if (prefix_to_mask(p) != mask) throw std::invalid_argument("non-contiguous subnet mask: " + to_string(mask));
    return p;
}

/// Smallest prefix whose subnet has >= hosts usable addresses (/31 and /32 excluded except hosts<=2 -> /30).
inline int prefix_for_hosts(unsigned long long hosts) {
    for (int p = 30; p >= 0; --p) {
        unsigned long long usable = (1ULL << (32 - p)) - 2;
        if (usable >= hosts) return p;
    }
    throw std::invalid_argument("too many hosts requested");
}

class Network {
  public:
    Network(uint32_t ip, int prefix) : prefix_(prefix) {
        if (prefix < 0 || prefix > 32) throw std::invalid_argument("prefix must be 0-32");
        addr_ = ip & prefix_to_mask(prefix);
    }

    static Network parse(const std::string& cidr) {
        auto slash = cidr.find('/');
        if (slash == std::string::npos) throw std::invalid_argument("expected CIDR like 192.168.1.0/24, got " + cidr);
        std::string pre = cidr.substr(slash + 1);
        int p = pre.find('.') != std::string::npos ? mask_to_prefix(parse_ip(pre)) : std::stoi(pre);
        return Network(parse_ip(cidr.substr(0, slash)), p);
    }

    uint32_t address() const { return addr_; }
    int prefix() const { return prefix_; }
    uint32_t mask() const { return prefix_to_mask(prefix_); }
    uint32_t wildcard() const { return ~mask(); }
    uint32_t broadcast() const { return addr_ | wildcard(); }
    unsigned long long size() const { return 1ULL << (32 - prefix_); }
    unsigned long long usable_hosts() const { return prefix_ >= 31 ? (prefix_ == 31 ? 2 : 1) : size() - 2; }
    uint32_t first_host() const { return prefix_ >= 31 ? addr_ : addr_ + 1; }
    uint32_t last_host() const { return prefix_ >= 31 ? broadcast() : broadcast() - 1; }
    bool contains(uint32_t ip) const { return (ip & mask()) == addr_; }
    bool contains(const Network& o) const { return o.prefix_ >= prefix_ && contains(o.addr_); }
    bool overlaps(const Network& o) const { return contains(o.addr_) || o.contains(addr_); }

    char ip_class() const {
        uint8_t first = addr_ >> 24;
        return first < 128 ? 'A' : first < 192 ? 'B' : first < 224 ? 'C' : first < 240 ? 'D' : 'E';
    }
    bool is_private() const {
        return Network(0x0A000000, 8).contains(addr_) || Network(0xAC100000, 12).contains(addr_) ||
               Network(0xC0A80000, 16).contains(addr_);
    }
    std::string str() const { return to_string(addr_) + "/" + std::to_string(prefix_); }

    /// Split into at least `parts` equal subnets (rounded up to a power of two).
    std::vector<Network> split(int parts) const {
        if (parts < 1) throw std::invalid_argument("parts must be >= 1");
        int bits = 0;
        while ((1 << bits) < parts) ++bits;
        if (prefix_ + bits > 32) throw std::invalid_argument("network too small to split that many times");
        std::vector<Network> out;
        unsigned long long step = 1ULL << (32 - prefix_ - bits);
        for (int i = 0; i < (1 << bits); ++i) out.emplace_back(addr_ + static_cast<uint32_t>(i * step), prefix_ + bits);
        return out;
    }

  private:
    uint32_t addr_;
    int prefix_;
};

struct Requirement {
    std::string name;
    unsigned long long hosts;
};

struct Allocation {
    Requirement req;
    Network net;
};

/// Classic VLSM: biggest requirement first, each placed at the next aligned free address.
inline std::vector<Allocation> vlsm(const Network& base, std::vector<Requirement> reqs) {
    std::stable_sort(reqs.begin(), reqs.end(), [](auto& a, auto& b) { return a.hosts > b.hosts; });
    std::vector<Allocation> out;
    unsigned long long cursor = base.address();
    const unsigned long long end = static_cast<unsigned long long>(base.broadcast()) + 1;
    for (auto& r : reqs) {
        int p = prefix_for_hosts(r.hosts);
        if (p < base.prefix())
            throw std::invalid_argument("'" + r.name + "' needs a /" + std::to_string(p) + ", bigger than " + base.str());
        unsigned long long size = 1ULL << (32 - p);
        cursor = (cursor + size - 1) / size * size;  // align
        if (cursor + size > end)
            throw std::invalid_argument("not enough space in " + base.str() + " for '" + r.name + "' (" +
                                        std::to_string(r.hosts) + " hosts)");
        out.push_back({r, Network(static_cast<uint32_t>(cursor), p)});
        cursor += size;
    }
    return out;
}

/// Largest aligned free CIDR block remaining after the last allocation.
inline std::optional<Network> next_free(const Network& base, const std::vector<Allocation>& plan) {
    unsigned long long cursor = base.address();
    for (auto& a : plan) cursor = std::max<unsigned long long>(cursor, (unsigned long long)a.net.broadcast() + 1);
    const unsigned long long end = static_cast<unsigned long long>(base.broadcast()) + 1;
    std::optional<Network> best;
    while (cursor < end) {  // decompose the free range into maximal aligned blocks
        int p = 32;
        while (p > base.prefix()) {
            unsigned long long size = 1ULL << (32 - (p - 1));
            if (cursor % size != 0 || cursor + size > end) break;
            --p;
        }
        Network blk(static_cast<uint32_t>(cursor), p);
        if (!best || blk.size() > best->size()) best = blk;
        cursor += blk.size();
    }
    return best;
}

}  // namespace sf
