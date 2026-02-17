#include "lan_backend.hpp"
#include "../wav_encoder.hpp"

#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <print>

using json = nlohmann::json;

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* resp = static_cast<std::string*>(userdata);
    resp->append(ptr, size * nmemb);
    return size * nmemb;
}

LanBackend::LanBackend(std::string url, std::string api_format, std::string language)
    : url_(std::move(url)), api_format_(std::move(api_format)),
      language_(std::move(language)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

LanBackend::~LanBackend() {
    curl_global_cleanup();
}

std::expected<TranscriptResult, std::string>
LanBackend::transcribe(std::span<const int16_t> audio, uint32_t sample_rate) {
    if (audio.empty()) {
        return std::unexpected("empty audio");
    }

    double duration_s = static_cast<double>(audio.size()) / sample_rate;

    // Encode to WAV
    auto wav_data = wav::encode(audio, sample_rate);

    auto start = std::chrono::steady_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected("curl_easy_init failed");
    }

    // Build URL and form based on API format
    std::string endpoint;
    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part;

    if (api_format_ == "openai") {
        endpoint = url_ + "/v1/audio/transcriptions";

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_data(part, reinterpret_cast<const char*>(wav_data.data()),
                       wav_data.size());
        curl_mime_filename(part, "audio.wav");
        curl_mime_type(part, "audio/wav");

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "model");
        curl_mime_data(part, "whisper-1", CURL_ZERO_TERMINATED);

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "language");
        curl_mime_data(part, language_.c_str(), CURL_ZERO_TERMINATED);

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "response_format");
        curl_mime_data(part, "json", CURL_ZERO_TERMINATED);
    } else {
        // whisper.cpp server format
        endpoint = url_ + "/inference";

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_data(part, reinterpret_cast<const char*>(wav_data.data()),
                       wav_data.size());
        curl_mime_filename(part, "audio.wav");
        curl_mime_type(part, "audio/wav");

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "temperature");
        curl_mime_data(part, "0.0", CURL_ZERO_TERMINATED);

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "response_format");
        curl_mime_data(part, "json", CURL_ZERO_TERMINATED);

        if (!language_.empty()) {
            part = curl_mime_addpart(mime);
            curl_mime_name(part, "language");
            curl_mime_data(part, language_.c_str(), CURL_ZERO_TERMINATED);
        }
    }

    std::string response_body;

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    auto end = std::chrono::steady_clock::now();
    double processing_s = std::chrono::duration<double>(end - start).count();

    if (res != CURLE_OK) {
        return std::unexpected(std::string("curl error: ") + curl_easy_strerror(res));
    }

    // Parse response
    try {
        auto j = json::parse(response_body);
        std::string text;

        if (j.contains("text")) {
            text = j["text"].get<std::string>();
        } else if (j.contains("error")) {
            return std::unexpected("server error: " + j["error"].get<std::string>());
        } else {
            return std::unexpected("unexpected response: " + response_body);
        }

        // Trim whitespace
        if (!text.empty()) {
            auto start_pos = text.find_first_not_of(" \t\n\r");
            auto end_pos = text.find_last_not_of(" \t\n\r");
            if (start_pos != std::string::npos) {
                text = text.substr(start_pos, end_pos - start_pos + 1);
            }
        }

        return TranscriptResult{
            .text = std::move(text),
            .duration_s = duration_s,
            .processing_s = processing_s,
        };
    } catch (const json::exception& e) {
        return std::unexpected(std::string("JSON parse error: ") + e.what());
    }
}
