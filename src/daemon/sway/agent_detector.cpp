#include "agent_detector.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

AgentDetector::AgentDetector(std::vector<std::string> known_agents)
    : known_agents_(std::move(known_agents)) {}

AgentDetector::DetectionResult AgentDetector::detect(int terminal_pid) const {
    if (terminal_pid <= 0) return {};

    DetectionResult result;
    search_tree(terminal_pid, result);
    return result;
}

std::string AgentDetector::read_comm(int pid) {
    std::ifstream f(std::format("/proc/{}/comm", pid));
    if (!f.is_open()) return {};
    std::string comm;
    std::getline(f, comm);
    return comm;
}

std::string AgentDetector::read_cwd(int pid) {
    std::error_code ec;
    auto path = fs::read_symlink(std::format("/proc/{}/cwd", pid), ec);
    if (ec) return {};
    return path.string();
}

std::vector<int> AgentDetector::get_children(int pid) {
    std::vector<int> children;

    // Read /proc/{pid}/task/*/children
    std::string task_path = std::format("/proc/{}/task", pid);
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(task_path, ec)) {
        auto children_file = entry.path() / "children";
        std::ifstream f(children_file);
        if (!f.is_open()) continue;

        int child;
        while (f >> child) {
            children.push_back(child);
        }
    }

    return children;
}

bool AgentDetector::search_tree(int pid, DetectionResult& result) const {
    auto children = get_children(pid);
    for (int child : children) {
        auto comm = read_comm(child);
        if (comm.empty()) continue;

        // Check if this is a known agent
        for (const auto& agent : known_agents_) {
            if (comm.find(agent) != std::string::npos) {
                result.agent = agent;
                result.working_dir = read_cwd(child);
                return true;
            }
        }

        // Recurse through shells and other intermediaries
        if (search_tree(child, result)) return true;
    }
    return false;
}

bool AgentDetector::is_shell(const std::string& comm) {
    static const std::vector<std::string> shells = {"bash", "zsh", "fish", "sh", "dash"};
    return std::ranges::any_of(shells, [&](const auto& s) { return comm == s; });
}
