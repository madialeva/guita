#pragma once
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>

// Number of TCP sockets in LISTEN state owned by this process (Linux): the
// socket inodes of /proc/self/fd matched against /proc/self/net/tcp{,6}
inline int listeningTcpSockets() {
    std::set<std::string> inodes;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code error;
        const auto target = std::filesystem::read_symlink(entry.path(), error).string();
        if (!error && target.starts_with("socket:["))
            inodes.insert(target.substr(8, target.size() - 9));
    }

    constexpr const char *kListen = "0A";
    int count = 0;
    for (const char *table : {"/proc/self/net/tcp", "/proc/self/net/tcp6"}) {
        std::ifstream in(table);
        std::string line;
        std::getline(in, line);      // header
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            std::string slot, local, remote, state, queues, timer, retransmits, uid, timeout, inode;
            fields >> slot >> local >> remote >> state >> queues >> timer >> retransmits >> uid >> timeout >> inode;
            if (state == kListen && inodes.contains(inode))
                ++count;
        }
    }
    return count;
}
