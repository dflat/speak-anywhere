#pragma once

#include "config.hpp"
#include "output/output.hpp"
#include "platform/audio_capture.hpp"
#include "platform/ipc_server.hpp"
#include "platform/process_detector.hpp"
#include "ring_buffer.hpp"
#include "session.hpp"
#include "storage/history_db.hpp"
#include "sway/window_info.hpp"
#include "whisper/backend.hpp"

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

class DaemonCore {
public:
    using OutputFactory = std::function<std::unique_ptr<OutputMethod>(const std::string&, bool)>;
    using NotifyCallback = std::function<void()>;

    DaemonCore(Config config, bool verbose,
               RingBuffer& ring_buf, AudioCapture& audio,
               ProcessDetector& detector, IpcServer& ipc,
               OutputFactory output_factory, NotifyCallback notify);
    ~DaemonCore();

    DaemonCore(const DaemonCore&) = delete;
    DaemonCore& operator=(const DaemonCore&) = delete;

    bool init();

    nlohmann::json handle_command(const std::string& cmd_str, const nlohmann::json& cmd);

    void on_transcription_complete();

    void add_waiting_client(int fd);
    void remove_waiting_client(int fd);

    void set_focused_window(const WindowInfo& info);

    SessionState session_state() const { return session_.state(); }

    void shutdown();

private:
    nlohmann::json handle_start(const nlohmann::json& cmd);
    nlohmann::json handle_stop(const nlohmann::json& cmd);
    nlohmann::json handle_toggle(const nlohmann::json& cmd);
    nlohmann::json handle_status(const nlohmann::json& cmd);
    nlohmann::json handle_history(const nlohmann::json& cmd);

    void start_transcription(std::vector<int16_t> audio, WindowInfo context,
                             const std::string& output_method);

    WindowInfo enrich_window_info(WindowInfo info);

    void log(const std::string& msg);

    Config config_;
    bool verbose_;

    RingBuffer& ring_buf_;
    AudioCapture& audio_;
    ProcessDetector& detector_;
    IpcServer& ipc_;

    OutputFactory output_factory_;
    NotifyCallback notify_;

    Session session_;
    HistoryDb history_db_;
    std::unique_ptr<WhisperBackend> backend_;

    WindowInfo focused_window_;
    std::string pending_output_method_;
    std::vector<int> waiting_clients_;

    struct WorkerResult {
        std::expected<TranscriptResult, std::string> result;
        WindowInfo context;
        std::string output_method;
    };
    WorkerResult worker_result_;
    std::jthread worker_;
};
