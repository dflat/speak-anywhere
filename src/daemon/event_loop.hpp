#pragma once

#include "audio_capture.hpp"
#include "config.hpp"
#include "ipc_server.hpp"
#include "output/clipboard_output.hpp"
#include "output/type_output.hpp"
#include "ring_buffer.hpp"
#include "session.hpp"
#include "storage/history_db.hpp"
#include "sway/agent_detector.hpp"
#include "sway/ipc.hpp"
#include "whisper/lan_backend.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class EventLoop {
public:
    explicit EventLoop(Config config, bool verbose = false);
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    bool init();
    void run();
    void request_stop();

private:
    // IPC command handlers
    nlohmann::json handle_start(const nlohmann::json& cmd);
    nlohmann::json handle_stop(const nlohmann::json& cmd);
    nlohmann::json handle_toggle(const nlohmann::json& cmd);
    nlohmann::json handle_status(const nlohmann::json& cmd);
    nlohmann::json handle_history(const nlohmann::json& cmd);

    // Start transcription in worker thread
    void start_transcription(std::vector<int16_t> audio, WindowInfo context,
                             const std::string& output_method);

    // Called when worker thread completes
    void on_transcription_complete();

    // Get output method by name
    std::unique_ptr<OutputMethod> make_output(const std::string& method, bool is_terminal);

    // Build context string like "claude code on kitty"
    WindowInfo enrich_window_info(WindowInfo info);

    void log(const std::string& msg);

    Config config_;
    bool verbose_;

    // Core components
    RingBuffer ring_buf_;
    AudioCapture audio_capture_;
    Session session_;

    // Subsystems
    IpcServer ipc_server_;
    SwayIpc sway_ipc_;
    AgentDetector agent_detector_;
    HistoryDb history_db_;
    std::unique_ptr<WhisperBackend> backend_;

    // Cached focused window
    WindowInfo focused_window_;

    // epoll
    int epoll_fd_ = -1;
    int signal_fd_ = -1;
    int worker_event_fd_ = -1;

    std::atomic<bool> running_{false};

    // Worker thread for transcription
    std::jthread worker_;
    struct WorkerResult {
        std::expected<TranscriptResult, std::string> result;
        WindowInfo context;
        std::string output_method;
    };
    WorkerResult worker_result_;

    // Pending output method for current recording
    std::string pending_output_method_;

    // Client FDs waiting for transcription result
    std::vector<int> waiting_clients_;
};
