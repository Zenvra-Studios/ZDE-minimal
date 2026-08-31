#include "WASAPIAudioOutput.h"

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace Zenvra::Drivers::Audio {

struct WASAPIAudioOutput::Impl {
    std::atomic<bool> running{false};
    std::thread worker_thread;
    int target_sample_rate = 44100;
    int target_channels = 2;
    RenderCallback callback;

    int actual_sample_rate = 44100;
    int actual_channels = 2;
    bool is_float_format = true;

    // Shared COM objects initialized synchronously in start(), used by audio_loop thread
    IAudioClient* shared_audio_client = nullptr;
    IAudioRenderClient* shared_render_client = nullptr;
    WAVEFORMATEX* shared_mix_format = nullptr;
    UINT32 shared_buffer_frame_count = 0;
    bool shared_co_inited = false;

    // Synchronous initialization: negotiate format and prepare WASAPI on the calling thread
    bool init_wasapi() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        shared_co_inited = SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || !enumerator) {
            std::cerr << "[WASAPI] CoCreateInstance for MMDeviceEnumerator failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr) || !device) {
            std::cerr << "[WASAPI] GetDefaultAudioEndpoint failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&shared_audio_client));
        if (FAILED(hr) || !shared_audio_client) {
            std::cerr << "[WASAPI] Activate IAudioClient failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        hr = shared_audio_client->GetMixFormat(&shared_mix_format);
        if (FAILED(hr) || !shared_mix_format) {
            std::cerr << "[WASAPI] GetMixFormat failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        // Use the system mix format directly — this is the native hardware rate
        // WASAPI shared mode requires using the mix format for guaranteed compatibility
        actual_sample_rate = static_cast<int>(shared_mix_format->nSamplesPerSec);
        actual_channels = static_cast<int>(shared_mix_format->nChannels);
        is_float_format = (shared_mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
            (shared_mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             reinterpret_cast<WAVEFORMATEXTENSIBLE*>(shared_mix_format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

        REFERENCE_TIME hns_buffer_duration = 500000; // 50ms
        hr = shared_audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hns_buffer_duration, 0, shared_mix_format, nullptr);
        if (FAILED(hr)) {
            std::cerr << "[WASAPI] IAudioClient::Initialize failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        hr = shared_audio_client->GetBufferSize(&shared_buffer_frame_count);
        if (FAILED(hr) || shared_buffer_frame_count == 0) {
            std::cerr << "[WASAPI] GetBufferSize failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        hr = shared_audio_client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&shared_render_client));
        if (FAILED(hr) || !shared_render_client) {
            std::cerr << "[WASAPI] GetService IAudioRenderClient failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        // Pre-fill buffer with silence
        BYTE* initial_data = nullptr;
        if (SUCCEEDED(shared_render_client->GetBuffer(shared_buffer_frame_count, &initial_data))) {
            shared_render_client->ReleaseBuffer(shared_buffer_frame_count, AUDCLNT_BUFFERFLAGS_SILENT);
        }

        hr = shared_audio_client->Start();
        if (FAILED(hr)) {
            std::cerr << "[WASAPI] IAudioClient::Start failed: hr=0x" << std::hex << hr << std::dec << '\n';
            cleanup_init(enumerator, device);
            return false;
        }

        // Release enumeration objects (audio_client and render_client stay alive for the thread)
        if (device) { device->Release(); device = nullptr; }
        if (enumerator) { enumerator->Release(); enumerator = nullptr; }

        std::cerr << "[WASAPI] Initialized: " << actual_sample_rate << " Hz, " << actual_channels << " ch, "
                  << (is_float_format ? "float32" : "int16") << '\n';
        return true;
    }

    void cleanup_init(IMMDeviceEnumerator* enumerator, IMMDevice* device) {
        if (shared_render_client) { shared_render_client->Release(); shared_render_client = nullptr; }
        if (shared_audio_client) { shared_audio_client->Stop(); shared_audio_client->Release(); shared_audio_client = nullptr; }
        if (shared_mix_format) { CoTaskMemFree(shared_mix_format); shared_mix_format = nullptr; }
        if (device) { device->Release(); }
        if (enumerator) { enumerator->Release(); }
        if (shared_co_inited) { CoUninitialize(); shared_co_inited = false; }
    }

    void audio_loop() {
        // COM already initialized on the main thread; re-initialize for this thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool co_inited = SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;

        std::vector<float> mix_temp;

        while (running.load(std::memory_order_relaxed)) {
            UINT32 padding_frames = 0;
            hr = shared_audio_client->GetCurrentPadding(&padding_frames);
            if (FAILED(hr)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            UINT32 frames_needed = shared_buffer_frame_count - padding_frames;
            if (frames_needed > 0) {
                BYTE* out_bytes = nullptr;
                hr = shared_render_client->GetBuffer(frames_needed, &out_bytes);
                if (SUCCEEDED(hr) && out_bytes) {
                    size_t total_samples = static_cast<size_t>(frames_needed) * actual_channels;

                    if (is_float_format) {
                        auto* float_out = reinterpret_cast<float*>(out_bytes);
                        if (callback) {
                            callback(std::span<float>(float_out, total_samples), frames_needed);
                        } else {
                            std::fill(float_out, float_out + total_samples, 0.0f);
                        }
                    } else {
                        // 16-bit integer PCM output fallback
                        mix_temp.resize(total_samples);
                        if (callback) {
                            callback(std::span<float>(mix_temp.data(), total_samples), frames_needed);
                        } else {
                            std::fill(mix_temp.begin(), mix_temp.end(), 0.0f);
                        }
                        auto* int16_out = reinterpret_cast<int16_t*>(out_bytes);
                        for (size_t i = 0; i < total_samples; ++i) {
                            float clamped = std::clamp(mix_temp[i], -1.0f, 1.0f);
                            int16_out[i] = static_cast<int16_t>(clamped * 32767.0f);
                        }
                    }

                    shared_render_client->ReleaseBuffer(frames_needed, 0);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Cleanup COM objects
        if (shared_render_client) { shared_render_client->Release(); shared_render_client = nullptr; }
        if (shared_audio_client) { shared_audio_client->Stop(); shared_audio_client->Release(); shared_audio_client = nullptr; }
        if (shared_mix_format) { CoTaskMemFree(shared_mix_format); shared_mix_format = nullptr; }
        if (co_inited) { CoUninitialize(); }
    }
};

WASAPIAudioOutput::WASAPIAudioOutput() : m_impl(std::make_unique<Impl>()) {}

WASAPIAudioOutput::~WASAPIAudioOutput() {
    stop();
}

bool WASAPIAudioOutput::start(int sample_rate, int channels, RenderCallback callback) {
    stop();

    m_impl->target_sample_rate = (sample_rate > 0) ? sample_rate : 44100;
    m_impl->target_channels = (channels > 0) ? channels : 2;
    m_impl->callback = std::move(callback);

    // Synchronously initialize WASAPI and negotiate format BEFORE starting render thread
    if (!m_impl->init_wasapi()) {
        return false;
    }

    m_impl->running.store(true, std::memory_order_relaxed);
    m_impl->worker_thread = std::thread(&Impl::audio_loop, m_impl.get());
    return true;
}

void WASAPIAudioOutput::stop() {
    if (m_impl && m_impl->running.load(std::memory_order_relaxed)) {
        m_impl->running.store(false, std::memory_order_relaxed);
        if (m_impl->worker_thread.joinable()) {
            m_impl->worker_thread.join();
        }
    }
}

bool WASAPIAudioOutput::is_running() const noexcept {
    return m_impl && m_impl->running.load(std::memory_order_relaxed);
}

int WASAPIAudioOutput::sample_rate() const noexcept {
    return m_impl ? m_impl->actual_sample_rate : 44100;
}

int WASAPIAudioOutput::channels() const noexcept {
    return m_impl ? m_impl->actual_channels : 2;
}

} // namespace Zenvra::Drivers::Audio

#endif // _WIN32

