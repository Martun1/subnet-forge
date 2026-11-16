// subnet-forge: CCNA subnetting & VLSM toolkit that writes Cisco IOS config for you.
// Author: Martin Ivanov (Martun1) | License: MIT | C++17, no external dependencies
#include "ipv4.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#define SF_ISATTY _isatty(0)
#else
#include <unistd.h>
#define SF_ISATTY isatty(0)
#endif

using namespace sf;

namespace col {
bool on = true;
const char* c(const char* code) { return on ? code : ""; }
const char* R() { return c("\033[0m"); }
const char* B() { return c("\033[1m"); }
const char* D() { return c("\033[2m"); }
const char* G() { return c("\033[92m"); }
const char* Y() { return c("\033[93m"); }
const char* C() { return c("\033[96m"); }
const char* M() { return c("\033[95m"); }
const char* Rd() { return c("\033[91m"); }
}  // namespace col

static void banner(const std::string& sub) {
    std::cout << col::B() << col::C() << "\n  ┌─┐┬ ┬┌┐ ┌┐┌┌─┐┌┬┐  ┌─┐┌─┐┬─┐┌─┐┌─┐\n"
              << "  └─┐│ │├┴┐│││├┤  │───├┤ │ │├┬┘│ ┬├┤ \n"
              << "  └─┘└─┘└─┘┘└┘└─┘ ┴   └  └─┘┴└─└─┘└─┘" << col::R() << col::D() << "  " << sub
              << col::R() << "\n\n";
}

static void row(const std::string& k, const std::string& v, const char* vc = "") {
    std::cout << "  " << col::D() << std::left << std::setw(18) << k << col::R() << vc << v << col::R() << "\n";
}

static std::string binary_mask(const Network& n) {
    std::string s, raw = to_binary(n.address());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '.') { s += '.'; continue; }
        size_t bit = i - std::count(raw.begin(), raw.begin() + i, '.');
        s += (bit < (size_t)n.prefix()) ? std::string(col::C()) + raw[i] + col::R()
                                        : std::string(col::Y()) + raw[i] + col::R();
    }
    return s;
}

// ------------------------------------------------------------------ info
static int cmd_info(const std::string& arg) {
    Network n = Network::parse(arg);
    banner("network info");
    row("Network", n.str(), col::B());
    row("Address class", std::string(1, n.ip_class()) + (n.is_private() ? "  (private RFC 1918)" : ""));
    row("Subnet mask", to_string(n.mask()));
    row("Wildcard mask", to_string(n.wildcard()));
    row("Network address", to_string(n.address()));
    row("Broadcast", to_string(n.broadcast()));
    row("First usable", to_string(n.first_host()), col::G());
    row("Last usable", to_string(n.last_host()), col::G());
    row("Usable hosts", std::to_string(n.usable_hosts()), col::B());
    std::cout << "  " << col::D() << std::left << std::setw(18) << "Binary" << col::R() << binary_mask(n) << "\n";
    std::cout << "  " << col::D() << std::setw(18) << "" << col::C() << "■ network bits  " << col::Y()
              << "■ host bits" << col::R() << "\n\n";
    std::cout << col::B() << "  Cisco IOS snippets" << col::R() << "\n";
    std::cout << col::D() << "  ! interface address\n" << col::R() << "  ip address " << to_string(n.first_host())
              << " " << to_string(n.mask()) << "\n";
    std::cout << col::D() << "  ! ACL / OSPF wildcard\n" << col::R() << "  access-list 10 permit "
              << to_string(n.address()) << " " << to_string(n.wildcard()) << "\n";
    std::cout << "  network " << to_string(n.address()) << " " << to_string(n.wildcard()) << " area 0\n\n";
    return 0;
}

// ------------------------------------------------------------------ split
static int cmd_split(const std::string& arg, int parts) {
    Network n = Network::parse(arg);
    auto subs = n.split(parts);
    banner("split " + n.str() + " into " + std::to_string(subs.size()) + " subnets");
    std::cout << col::B() << "  #   Subnet               Mask              Usable range                       Hosts\n"
              << col::R() << col::D() << "  " << std::string(92, '-') << col::R() << "\n";
    int i = 1;
    for (auto& s : subs) {
        std::ostringstream range;
        range << to_string(s.first_host()) << " - " << to_string(s.last_host());
        std::cout << "  " << std::left << std::setw(4) << i++ << col::C() << std::setw(21) << s.str() << col::R()
                  << std::setw(18) << to_string(s.mask()) << std::setw(35) << range.str() << col::G()
                  << s.usable_hosts() << col::R() << "\n";
    }
    std::cout << "\n";
    return 0;
}

// ------------------------------------------------------------------ vlsm
static int cmd_vlsm(const std::string& base, const std::vector<std::string>& reqs, bool ios,
                    const std::string& iface_prefix, const std::string& out_file) {
    Network n = Network::parse(base);
    std::vector<Requirement> want;
    for (auto& r : reqs) {
        auto p = r.find(':');
        if (p == std::string::npos) throw std::invalid_argument("requirement must look like NAME:HOSTS, got '" + r + "'");
        want.push_back({r.substr(0, p), std::stoul(r.substr(p + 1))});
    }
    auto plan = vlsm(n, want);
    banner("VLSM plan for " + n.str());
    std::cout << col::B() << "  Name         Need   Subnet               Mask              Gateway          Usable  Waste\n"
              << col::R() << col::D() << "  " << std::string(98, '-') << col::R() << "\n";
    unsigned long long used = 0;
    for (auto& a : plan) {
        unsigned long long waste = a.net.usable_hosts() - a.req.hosts;
        used += a.net.size();
        std::cout << "  " << col::B() << std::left << std::setw(13) << a.req.name << col::R() << std::setw(7)
                  << a.req.hosts << col::C() << std::setw(21) << a.net.str() << col::R() << std::setw(18)
                  << to_string(a.net.mask()) << col::G() << std::setw(17) << to_string(a.net.first_host())
                  << col::R() << std::setw(8) << a.net.usable_hosts() << (waste > a.req.hosts ? col::Y() : col::D())
                  << waste << col::R() << "\n";
    }
    double pct = 100.0 * used / n.size();
    int bars = (int)(pct / 100 * 40);
    std::cout << "\n  Address space used: " << col::G();
    for (int i = 0; i < 40; ++i) std::cout << (i < bars ? "█" : "░");
    std::cout << col::R() << " " << std::fixed << std::setprecision(1) << pct << "%  (" << used << "/" << n.size()
              << " addresses)\n";
    if (auto free_net = next_free(n, plan)) {
        std::cout << "  Next free block:    " << col::C() << free_net->str() << col::R() << col::D()
                  << "  ← keep for future growth" << col::R() << "\n";
    }
    std::cout << "\n";

    if (ios || !out_file.empty()) {
        std::ostringstream cfg;
        cfg << "! ===== Generated by subnet-forge for " << n.str() << " =====\n";
        int vlan = 10;
        for (auto& a : plan) {
            cfg << "!\n! --- " << a.req.name << " (" << a.req.hosts << " hosts) ---\n";
            cfg << "interface " << iface_prefix << "." << vlan << "\n";
            cfg << " description " << a.req.name << "\n";
            cfg << " encapsulation dot1Q " << vlan << "\n";
            cfg << " ip address " << to_string(a.net.first_host()) << " " << to_string(a.net.mask()) << "\n";
            cfg << "!\n";
            cfg << "ip dhcp excluded-address " << to_string(a.net.first_host()) << "\n";
            cfg << "ip dhcp pool " << a.req.name << "\n";
            cfg << " network " << to_string(a.net.address()) << " " << to_string(a.net.mask()) << "\n";
            cfg << " default-router " << to_string(a.net.first_host()) << "\n";
            cfg << " dns-server 1.1.1.1 8.8.8.8\n";
            vlan += 10;
        }
        cfg << "!\nrouter ospf 1\n";
        for (auto& a : plan)
            cfg << " network " << to_string(a.net.address()) << " " << to_string(a.net.wildcard()) << " area 0\n";
        cfg << "end\n";
        if (ios) {
            std::cout << col::B() << "  Cisco IOS configuration (router-on-a-stick + DHCP + OSPF)\n" << col::R();
            std::istringstream is(cfg.str());
            std::string line;
            while (std::getline(is, line))
                std::cout << "  " << (line.rfind('!', 0) == 0 ? col::D() : "") << line << col::R() << "\n";
            std::cout << "\n";
        }
        if (!out_file.empty()) {
            std::ofstream(out_file) << cfg.str();
            std::cout << "  " << col::G() << "[+]" << col::R() << " Config written to " << out_file << "\n\n";
        }
    }
    return 0;
}

// ------------------------------------------------------------------ quiz
static int cmd_quiz(int rounds, unsigned seed) {
    std::mt19937 rng(seed ? seed : (unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    banner("subnetting quiz · " + std::to_string(rounds) + " questions");
    int score = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int q = 1; q <= rounds; ++q) {
        uint32_t ip = (192u << 24) | (168u << 16) | ((rng() % 256) << 8) | (rng() % 256);
        if (rng() % 3 == 0) ip = (10u << 24) | (rng() & 0xFFFFFF);
        int prefix = 20 + rng() % 11;  // /20 - /30
        Network n(ip, prefix);
        int kind = rng() % 4;
        std::string question, answer;
        switch (kind) {
            case 0: question = "Network address of " + to_string(ip) + "/" + std::to_string(prefix);
                    answer = to_string(n.address()); break;
            case 1: question = "Broadcast address of " + to_string(ip) + "/" + std::to_string(prefix);
                    answer = to_string(n.broadcast()); break;
            case 2: question = "Usable hosts in a /" + std::to_string(prefix);
                    answer = std::to_string(n.usable_hosts()); break;
            default: question = "Wildcard mask for /" + std::to_string(prefix);
                    answer = to_string(n.wildcard()); break;
        }
        std::cout << "  " << col::M() << "Q" << q << col::R() << "  " << question << "\n  > " << std::flush;
        std::string in;
        if (!std::getline(std::cin, in)) break;
        if (!SF_ISATTY) std::cout << in << "\n";  // echo piped answers (demos / CI)
        in.erase(std::remove_if(in.begin(), in.end(), ::isspace), in.end());
        if (in == answer) {
            ++score;
            std::cout << "  " << col::G() << "✔ correct" << col::R() << "\n\n";
        } else {
            std::cout << "  " << col::Rd() << "✘ " << col::R() << "answer: " << col::B() << answer << col::R()
                      << col::D() << "   (" << n.str() << " → " << to_string(n.address()) << " - "
                      << to_string(n.broadcast()) << ")" << col::R() << "\n\n";
        }
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "  " << col::B() << "Score: " << score << "/" << rounds << col::R() << "  in " << secs << "s  "
              << (score == rounds ? "🏆 CCNA-ready!" : score * 2 >= rounds ? "👍 keep practising" : "📚 review the binary method")
              << "\n\n";
    return 0;
}

static void usage() {
    std::cout << "subnet-forge: CCNA subnetting & VLSM toolkit\n\n"
                 "Usage:\n"
                 "  subnet-forge info  <ip/prefix | ip mask>          details, binary view, IOS snippets\n"
                 "  subnet-forge split <network/prefix> <N>          split into N equal subnets\n"
                 "  subnet-forge vlsm  <network/prefix> NAME:HOSTS... [--ios] [--iface G0/0] [-o file]\n"
                 "  subnet-forge quiz  [rounds] [--seed N]            practise for the CCNA exam\n\n"
                 "Options:  --no-color   disable colours\n\n"
                 "Examples:\n"
                 "  subnet-forge info 172.16.45.200/21\n"
                 "  subnet-forge vlsm 10.10.0.0/22 Sales:200 IT:60 Guest:100 Servers:14 WAN:2 --ios\n";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // box-drawing characters
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
#endif
    std::vector<std::string> a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-color") == 0) col::on = false;
        else a.emplace_back(argv[i]);
    }
    if (a.empty() || a[0] == "-h" || a[0] == "--help") { usage(); return a.empty() ? 1 : 0; }
    try {
        const std::string& cmd = a[0];
        if (cmd == "info" && a.size() >= 2) {
            std::string arg = a[1];
            if (a.size() >= 3 && arg.find('/') == std::string::npos) arg += "/" + std::to_string(mask_to_prefix(parse_ip(a[2])));
            return cmd_info(arg);
        }
        if (cmd == "split" && a.size() >= 3) return cmd_split(a[1], std::stoi(a[2]));
        if (cmd == "vlsm" && a.size() >= 3) {
            bool ios = false;
            std::string iface = "GigabitEthernet0/0", out;
            std::vector<std::string> reqs;
            for (size_t i = 2; i < a.size(); ++i) {
                if (a[i] == "--ios") ios = true;
                else if (a[i] == "--iface" && i + 1 < a.size()) iface = a[++i];
                else if (a[i] == "-o" && i + 1 < a.size()) out = a[++i];
                else reqs.push_back(a[i]);
            }
            return cmd_vlsm(a[1], reqs, ios, iface, out);
        }
        if (cmd == "quiz") {
            int rounds = 5; unsigned seed = 0;
            for (size_t i = 1; i < a.size(); ++i) {
                if (a[i] == "--seed" && i + 1 < a.size()) seed = std::stoul(a[++i]);
                else rounds = std::stoi(a[i]);
            }
            return cmd_quiz(rounds, seed);
        }
        usage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << col::Rd() << "error: " << col::R() << e.what() << "\n";
        return 2;
    }
}
