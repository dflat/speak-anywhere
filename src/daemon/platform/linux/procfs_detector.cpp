#include "platform/linux/procfs_detector.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

namespace fs = std::filesystem;

ProcfsDetector::ProcfsDetector(std::vector<std::string> known_agents)
    : known_agents_(std::move(known_agents)) {}

DetectionResult ProcfsDetector::detect(int pid) const {
    if (pid <= 0) return {};

    DetectionResult result;
    search_tree(pid, result);
    return result;
}

std::string ProcfsDetector::read_comm(int pid) {
    std::ifstream f(std::format("/proc/{}/comm", pid));
    if (!f.is_open()) return {};
    std::string comm;
    std::getline(f, comm);
    return comm;
}

std::string ProcfsDetector::read_cwd(int pid) {
    std::error_code ec;
    auto path = fs::read_symlink(std::format("/proc/{}/cwd", pid), ec);
    if (ec) return {};
    return path.string();
}

std::vector<int> ProcfsDetector::get_children(int pid) {
    std::vector<int> children;

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

bool ProcfsDetector::search_tree(int pid, DetectionResult& result) const {
    auto children = get_children(pid);
    for (int child : children) {
        auto comm = read_comm(child);
        if (comm.empty()) continue;

        for (const auto& agent : known_agents_) {
            if (comm.find(agent) != std::string::npos) {
                result.agent = agent;
                result.working_dir = read_cwd(child);
                return true;
            }
        }

        if (search_tree(child, result)) return true;
    }
    return false;
}
