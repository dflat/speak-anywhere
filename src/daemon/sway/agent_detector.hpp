#pragma once

#include <string>
#include <vector>

class AgentDetector {
public:
    explicit AgentDetector(std::vector<std::string> known_agents);

    struct DetectionResult {
        std::string agent;       // e.g. "claude"
        std::string working_dir; // agent's cwd
    };

    // From a terminal PID, walk the process tree to find a known CLI agent.
    DetectionResult detect(int terminal_pid) const;

private:
    // Read /proc/{pid}/comm, return empty on failure.
    static std::string read_comm(int pid);

    // Read /proc/{pid}/cwd symlink, return empty on failure.
    static std::string read_cwd(int pid);

    // Get child PIDs of a process by reading /proc/{pid}/task/*/children.
    static std::vector<int> get_children(int pid);

    // Recursively search process tree for a known agent.
    bool search_tree(int pid, DetectionResult& result) const;

    // Is this a shell process we should skip through?
    static bool is_shell(const std::string& comm);

    std::vector<std::string> known_agents_;
};
