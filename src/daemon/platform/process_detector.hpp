#pragma once

#include <string>

struct DetectionResult {
    std::string agent;
    std::string working_dir;
};

class ProcessDetector {
public:
    virtual ~ProcessDetector() = default;
    virtual DetectionResult detect(int pid) const = 0;
};
