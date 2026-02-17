#include <catch2/catch_test_macros.hpp>

#include "sway/agent_detector.hpp"

#include <sys/wait.h>
#include <unistd.h>

TEST_CASE("AgentDetector", "[agent]") {

    SECTION("DetectSelf") {
        // Get our own comm name and use it as a "known agent"
        auto self_pid = getpid();
        // Read our own /proc/self/comm
        std::string our_comm;
        if (auto f = fopen("/proc/self/comm", "r")) {
            char buf[256];
            if (fgets(buf, sizeof(buf), f)) {
                our_comm = buf;
                // Strip trailing newline
                if (!our_comm.empty() && our_comm.back() == '\n')
                    our_comm.pop_back();
            }
            fclose(f);
        }
        REQUIRE_FALSE(our_comm.empty());

        // Detect from parent PID — our process should be found as a child of parent
        AgentDetector detector({our_comm});
        auto result = detector.detect(getppid());
        REQUIRE(result.agent == our_comm);
        REQUIRE_FALSE(result.working_dir.empty());
    }

    SECTION("DetectChildProcess") {
        // Fork a child that sleeps, detect it from our PID
        pid_t child = fork();
        REQUIRE(child >= 0);

        if (child == 0) {
            // Child: sleep until parent is done
            sleep(10);
            _exit(0);
        }

        // Parent: give child time to start
        usleep(50000); // 50ms

        // Our child's comm is our comm (fork preserves it).
        // Read child's comm.
        std::string child_comm;
        {
            auto path = "/proc/" + std::to_string(child) + "/comm";
            if (auto f = fopen(path.c_str(), "r")) {
                char buf[256];
                if (fgets(buf, sizeof(buf), f)) {
                    child_comm = buf;
                    if (!child_comm.empty() && child_comm.back() == '\n')
                        child_comm.pop_back();
                }
                fclose(f);
            }
        }

        AgentDetector detector({child_comm});
        auto result = detector.detect(getpid());
        REQUIRE(result.agent == child_comm);

        // Cleanup
        kill(child, SIGTERM);
        waitpid(child, nullptr, 0);
    }

    SECTION("NoMatchReturnsEmpty") {
        AgentDetector detector({"definitely_not_a_real_process_name_xyz"});
        auto result = detector.detect(getpid());
        REQUIRE(result.agent.empty());
        REQUIRE(result.working_dir.empty());
    }

    SECTION("InvalidPidReturnsEmpty") {
        AgentDetector detector({"anything"});

        auto r1 = detector.detect(0);
        REQUIRE(r1.agent.empty());

        auto r2 = detector.detect(-1);
        REQUIRE(r2.agent.empty());
    }
}
