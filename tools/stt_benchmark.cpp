#include <whisper.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>

namespace {

struct Audio {
    std::vector<float> samples;
    int sampleRate{0};
    int channels{0};
};

template <typename T>
bool read(std::ifstream& file, T& value)
{
    return static_cast<bool>(file.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

Audio loadWav(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    char riff[4]{};
    char wave[4]{};
    uint32_t riffSize{};
    if (!file || !file.read(riff, 4) || !read(file, riffSize) || !file.read(wave, 4)
        || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
        throw std::runtime_error("not a RIFF/WAVE file: " + path);
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<std::int16_t> pcm;
    while (file) {
        char chunkId[4]{};
        uint32_t chunkSize{};
        if (!file.read(chunkId, 4) || !read(file, chunkSize)) break;
        const std::streamoff chunkStart = file.tellg();
        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t blockAlign = 0;
            uint32_t byteRate = 0;
            if (!read(file, format) || !read(file, channels) || !read(file, sampleRate)
                || !read(file, byteRate) || !read(file, blockAlign) || !read(file, bitsPerSample)) {
                throw std::runtime_error("truncated fmt chunk: " + path);
            }
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (chunkSize % sizeof(std::int16_t) != 0) {
                throw std::runtime_error("non-PCM16 data chunk: " + path);
            }
            pcm.resize(chunkSize / sizeof(std::int16_t));
            if (!file.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(chunkSize))) {
                throw std::runtime_error("truncated data chunk: " + path);
            }
        }
        file.seekg(chunkStart + static_cast<std::streamoff>(chunkSize) + (chunkSize & 1U));
    }
    if (format != 1 || channels == 0 || sampleRate == 0 || bitsPerSample != 16 || pcm.empty()) {
        throw std::runtime_error("only PCM16 WAV is supported: " + path);
    }

    const std::size_t frameCount = pcm.size() / channels;
    std::vector<float> mono(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        double value = 0.0;
        for (uint16_t channel = 0; channel < channels; ++channel) {
            value += pcm[frame * channels + channel] / 32768.0;
        }
        mono[frame] = static_cast<float>(value / channels);
    }

    const std::size_t outputFrames = frameCount * 16'000 / sampleRate;
    Audio audio;
    audio.samples.resize(outputFrames);
    audio.sampleRate = 16'000;
    audio.channels = 1;
    for (std::size_t index = 0; index < outputFrames; ++index) {
        const double position = static_cast<double>(index) * sampleRate / 16'000.0;
        const std::size_t left = std::min(static_cast<std::size_t>(position), frameCount - 1);
        const std::size_t right = std::min(left + 1, frameCount - 1);
        const double fraction = position - left;
        audio.samples[index] = static_cast<float>(mono[left] * (1.0 - fraction) + mono[right] * fraction);
    }
    return audio;
}

double nowMs()
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Options {
    std::string model;
    std::vector<std::string> files;
    int threads{8};
    int maxTokens{48};
    bool useGpu{true};
    bool flashAttention{true};
    bool warmup{true};
};

Options parse(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto next = [&]() -> std::string {
            if (index + 1 >= argc) throw std::runtime_error("missing value for " + argument);
            return argv[++index];
        };
        if (argument == "--model") options.model = next();
        else if (argument == "--threads") options.threads = std::stoi(next());
        else if (argument == "--max-tokens") options.maxTokens = std::stoi(next());
        else if (argument == "--no-gpu") options.useGpu = false;
        else if (argument == "--no-flash") options.flashAttention = false;
        else if (argument == "--no-warmup") options.warmup = false;
        else if (argument == "--help") {
            std::cout << "Usage: RaceEngineerSttBench --model MODEL [--threads 4|6|8] "
                         "[--max-tokens N] [--no-gpu] [--no-flash] [--no-warmup] FILE.wav...\n";
            std::exit(0);
        } else options.files.push_back(argument);
    }
    if (options.model.empty() || options.files.empty()) {
        throw std::runtime_error("model and at least one WAV file are required; use --help");
    }
    return options;
}

whisper_full_params makeParams(const Options& options, int maxTokens)
{
    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = options.threads;
    params.translate = false;
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.max_tokens = maxTokens;
    params.audio_ctx = 768;
    params.temperature_inc = 0.0F;
    params.greedy.best_of = 1;
    params.language = "vi";
    params.detect_language = false;
    params.initial_prompt = "Vietnamese race engineer. Preserve English racing terms exactly: "
                            "gap ahead, full push, box this lap, tyre, tire, pit, fuel, sector, delta, "
                            "brake bias, understeer, oversteer, front left, front right, rear left, "
                            "rear right, DRS, ERS, ABS, TC.";
    return params;
}

void printTimings(const std::string& file, double totalMs, const whisper_timings* timings)
{
    const double sampleMs = timings == nullptr ? 0.0 : timings->sample_ms;
    const double encodeMs = timings == nullptr ? 0.0 : timings->encode_ms;
    const double decodeMs = timings == nullptr ? 0.0 : timings->decode_ms;
    const double batchMs = timings == nullptr ? 0.0 : timings->batchd_ms;
    const double promptMs = timings == nullptr ? 0.0 : timings->prompt_ms;
    const double backendMs = std::max(0.0, totalMs - sampleMs - encodeMs - decodeMs - batchMs - promptMs);
    std::cout << std::fixed << std::setprecision(2)
              << "file=" << file << " total_ms=" << totalMs
              << " sample_ms=" << sampleMs << " encoder_ms=" << encodeMs
              << " decoder_ms=" << decodeMs << " batch_decoder_ms=" << batchMs
              << " prompt_ms=" << promptMs
              << " gpu_sync_transfer_estimate_ms=" << backendMs << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parse(argc, argv);
        std::vector<Audio> audio;
        audio.reserve(options.files.size());
        for (const auto& file : options.files) audio.push_back(loadWav(file));

        auto contextParams = whisper_context_default_params();
        contextParams.use_gpu = options.useGpu;
        contextParams.flash_attn = options.flashAttention;
        contextParams.gpu_device = 0;
        const double loadStart = nowMs();
        whisper_context* context = whisper_init_from_file_with_params(options.model.c_str(), contextParams);
        if (context == nullptr) throw std::runtime_error("whisper model load failed");
        std::cout << std::fixed << std::setprecision(2)
                  << "model_load_ms=" << (nowMs() - loadStart)
                  << " threads=" << options.threads
                  << " gpu=" << options.useGpu
                  << " flash_attn=" << options.flashAttention << '\n';

        if (options.warmup) {
            std::vector<float> silence(16'000, 0.0F);
            auto params = makeParams(options, 8);
            whisper_reset_timings(context);
            const double start = nowMs();
            const int result = whisper_full(context, params, silence.data(), static_cast<int>(silence.size()));
            std::cout << "warmup_result=" << result << " warmup_ms=" << (nowMs() - start) << '\n';
        }

        for (std::size_t index = 0; index < audio.size(); ++index) {
            auto params = makeParams(options, options.maxTokens);
            whisper_reset_timings(context);
            const double start = nowMs();
            const int result = whisper_full(context, params, audio[index].samples.data(),
                                            static_cast<int>(audio[index].samples.size()));
            const double elapsed = nowMs() - start;
            whisper_timings* timings = whisper_get_timings(context);
            if (result != 0) {
                std::cerr << "transcribe_failed file=" << options.files[index] << " result=" << result << '\n';
            }
            printTimings(options.files[index], elapsed, timings);
            delete timings;
            std::string text;
            for (int segment = 0; segment < whisper_full_n_segments(context); ++segment) {
                text += whisper_full_get_segment_text(context, segment);
            }
            std::cout << "text=" << text << '\n';
        }
        whisper_free(context);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
