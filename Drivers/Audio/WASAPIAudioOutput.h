#pragma once

#ifdef _WIN32
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <thread>

namespace Zenvra::Drivers::Audio {

class WASAPIAudioOutput {
public:
    // Callback to fetch mixed interleaved float PCM samples: (output_span, frame_count)
    using RenderCallback = std::function<void(std::span<float>, size_t)>;

    WASAPIAudioOutput();
    ~WASAPIAudioOutput();

    WASAPIAudioOutput(const WASAPIAudioOutput&) = delete;
    WASAPIAudioOutput& operator=(const WASAPIAudioOutput&) = delete;

    bool start(int sample_rate, int channels, RenderCallback callback);
    void stop();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] int sample_rate() const noexcept;
    [[nodiscard]] int channels() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Zenvra::Drivers::Audio

#endif // _WIN32
