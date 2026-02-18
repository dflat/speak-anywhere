#pragma once

#include "platform/process_detector.hpp"

#include <string>
#include <vector>

class ProcfsDetector : public ProcessDetector {
public:
    explicit ProcfsDetector(std::vector<std::string> known_agents);

    DetectionResult detect(int pid) const override;

private:
    static std::string read_comm(int pid);
    static std::string read_cwd(int pid);
    static std::vector<int> get_children(int pid);
    bool search_tree(int pid, DetectionResult& result) const;

    std::vector<std::string> known_agents_;
};
