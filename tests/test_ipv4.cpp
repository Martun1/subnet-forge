// Minimal self-contained unit tests (no framework needed). Run: ctest  or  ./test_ipv4
#include "../src/ipv4.hpp"

#include <iostream>

using namespace sf;
static int fails = 0, total = 0;
#define CHECK(expr)                                                                   \
    do {                                                                              \
        ++total;                                                                      \
        if (!(expr)) { ++fails; std::cerr << "FAIL line " << __LINE__ << ": " #expr "\n"; } \
    } while (0)
#define THROWS(expr)                                                                  \
    do {                                                                              \
        ++total; bool t = false;                                                      \
        try { expr; } catch (const std::exception&) { t = true; }                     \
        if (!t) { ++fails; std::cerr << "FAIL (no throw) line " << __LINE__ << "\n"; } \
    } while (0)

int main() {
    // parsing
    CHECK(parse_ip("192.168.1.10") == 0xC0A8010A);
    THROWS(parse_ip("256.1.1.1"));
    THROWS(parse_ip("1.2.3"));
    CHECK(mask_to_prefix(parse_ip("255.255.255.192")) == 26);
    THROWS(mask_to_prefix(parse_ip("255.0.255.0")));

    // classic CCNA example: 172.16.45.200/21
    auto n = Network::parse("172.16.45.200/21");
    CHECK(to_string(n.address()) == "172.16.40.0");
    CHECK(to_string(n.broadcast()) == "172.16.47.255");
    CHECK(n.usable_hosts() == 2046);
    CHECK(to_string(n.wildcard()) == "0.0.7.255");
    CHECK(n.ip_class() == 'B' && n.is_private());

    // dotted mask form
    CHECK(Network::parse("10.1.1.1/255.255.255.252").prefix() == 30);
    CHECK(Network::parse("10.1.1.0/30").usable_hosts() == 2);

    // split
    auto s = Network::parse("192.168.10.0/24").split(4);
    CHECK(s.size() == 4 && s[3].str() == "192.168.10.192/26");
    CHECK(Network::parse("192.168.10.0/24").split(3).size() == 4);

    // host -> prefix
    CHECK(prefix_for_hosts(2) == 30);
    CHECK(prefix_for_hosts(50) == 26);
    CHECK(prefix_for_hosts(126) == 25);
    CHECK(prefix_for_hosts(127) == 24);

    // VLSM
    auto plan = vlsm(Network::parse("10.10.0.0/22"), {{"WAN", 2}, {"Sales", 200}, {"IT", 60}, {"Guest", 100}});
    CHECK(plan[0].req.name == "Sales" && plan[0].net.str() == "10.10.0.0/24");
    CHECK(plan[1].net.str() == "10.10.1.0/25");
    CHECK(plan[2].net.str() == "10.10.1.128/26");
    CHECK(plan[3].net.str() == "10.10.1.192/30");
    for (size_t i = 0; i < plan.size(); ++i)
        for (size_t j = i + 1; j < plan.size(); ++j) CHECK(!plan[i].net.overlaps(plan[j].net));
    auto f = next_free(Network::parse("10.10.0.0/22"), plan);
    CHECK(f && f->str() == "10.10.2.0/23");
    THROWS(vlsm(Network::parse("192.168.1.0/24"), {{"Big", 300}}));

    std::cout << (total - fails) << "/" << total << " checks passed\n";
    return fails ? 1 : 0;
}
