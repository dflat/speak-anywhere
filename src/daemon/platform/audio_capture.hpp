#pragma once

class AudioCapture {
public:
    virtual ~AudioCapture() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool is_capturing() const = 0;
};
