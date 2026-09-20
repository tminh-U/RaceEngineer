#include "vieneu/vieneu_tts.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <windows.h>

namespace fs = std::filesystem;

struct Phrase {
    std::string id;
    std::string text;
};

static std::vector<Phrase> loadPhrases(const fs::path& phrasesJsonPath)
{
    std::ifstream file(phrasesJsonPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open phrases file: " << phrasesJsonPath << "\n";
        return {};
    }

    std::vector<Phrase> phrases;
    std::string line;
    Phrase cur;
    while (std::getline(file, line)) {
        auto idPos = line.find("\"id\": \"");
        if (idPos != std::string::npos) {
            auto start = idPos + 7;
            auto end = line.find('"', start);
            if (end != std::string::npos) cur.id = line.substr(start, end - start);
        }
        auto textPos = line.find("\"text\": \"");
        if (textPos != std::string::npos) {
            auto start = textPos + 9;
            auto end = line.find('"', start);
            if (end != std::string::npos) {
                cur.text = line.substr(start, end - start);
                if (!cur.id.empty() && !cur.text.empty()) {
                    phrases.push_back(cur);
                    cur = {};
                }
            }
        }
    }
    return phrases;
}

static bool writeWavFile(const fs::path& filePath, const int16_t* pcm, int nSamples, int sampleRate)
{
    FILE* fp = _wfopen(filePath.c_str(), L"wb");
    if (!fp) return false;

    uint32_t dataBytes = static_cast<uint32_t>(nSamples * sizeof(int16_t));
    uint32_t totalBytes = 36 + dataBytes;
    uint32_t sRate = static_cast<uint32_t>(sampleRate);
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign = channels * (bitsPerSample / 8);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1; // PCM

    fwrite("RIFF", 1, 4, fp);
    fwrite(&totalBytes, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    fwrite(&subchunk1Size, 4, 1, fp);
    fwrite(&audioFormat, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    fwrite(&sRate, 4, 1, fp);
    fwrite(&byteRate, 4, 1, fp);
    fwrite(&blockAlign, 2, 1, fp);
    fwrite(&bitsPerSample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&dataBytes, 4, 1, fp);
    fwrite(pcm, sizeof(int16_t), nSamples, fp);

    fclose(fp);
    return true;
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== VieNeu-TTS Spotter Cache Generator (Voice: Minh Quân) ===\n");

    fs::path current = fs::current_path();
    fs::path projectRoot;
    if (fs::exists(current / "scripts" / "spotter_phrases.json")) {
        projectRoot = current;
    } else if (fs::exists(current.parent_path() / "scripts" / "spotter_phrases.json")) {
        projectRoot = current.parent_path();
    } else {
        projectRoot = current;
    }

    fs::path phrasesPath = projectRoot / "scripts" / "spotter_phrases.json";
    fs::path modelDir = projectRoot / "models" / "vieneu-v3";
    fs::path codecDir = modelDir / "codec";
    fs::path outputDir = projectRoot / "assets" / "spotter";
    fs::path buildOutputDir = projectRoot / "build" / "audio" / "spotter";

    auto phrases = loadPhrases(phrasesPath);
    if (phrases.empty()) {
        fprintf(stderr, "Error: No phrases loaded from %ls\n", phrasesPath.c_str());
        return 1;
    }
    printf("Loaded %zu phrases from %ls\n", phrases.size(), phrasesPath.c_str());

    constexpr int kVariants = 15;
    const size_t totalFiles = phrases.size() * kVariants;

    // Initialize VieNeu engine
    struct vieneu_init_params_v2 init_params;
    vieneu_init_v2_default_params(&init_params);
    init_params.profile = "vieneu-v3-native";
    std::string modelDirStr = modelDir.string();
    std::string codecDirStr = codecDir.string();
    init_params.model_dir = modelDirStr.c_str();
    init_params.codec_dir = codecDirStr.c_str();
    init_params.n_threads = 4;

    printf("Initializing VieNeu engine...\n");
    auto t0 = std::chrono::high_resolution_clock::now();
    struct vieneu_context* ctx = vieneu_init_v2(&init_params);
    auto t1 = std::chrono::high_resolution_clock::now();
    if (!ctx) {
        fprintf(stderr, "Failed to initialize VieNeu context: %s\n", vieneu_last_error());
        return 1;
    }
    double initMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("VieNeu engine ready in %.2f ms\n", initMs);

    // Clean and ensure directories exist
    fs::create_directories(outputDir);
    fs::create_directories(buildOutputDir);

    size_t completed = 0;
    auto genStart = std::chrono::high_resolution_clock::now();

    for (const auto& phrase : phrases) {
        fs::path phraseDir = outputDir / phrase.id;
        fs::path buildPhraseDir = buildOutputDir / phrase.id;
        fs::create_directories(phraseDir);
        fs::create_directories(buildPhraseDir);

        for (int variant = 1; variant <= kVariants; ++variant) {
            char filename[16];
            snprintf(filename, sizeof(filename), "%02d.wav", variant);
            fs::path targetWav = phraseDir / filename;
            fs::path buildWav = buildPhraseDir / filename;

            struct vieneu_tts_params_v3 synth_params;
            vieneu_tts_v3_default_params(&synth_params);
            synth_params.text = phrase.text.c_str();
            synth_params.voice_id = "Minh Quân";
            synth_params.style = "tu_nhien";
            synth_params.ref_audio_path = nullptr;
            synth_params.use_ref_codes = true;
            synth_params.denoise_ref = false;

            // Vary temperature and top_p across variants for natural prosody changes
            synth_params.temperature = 0.30f + ((variant - 1) % 6) * 0.05f;
            synth_params.top_p = 0.86f + (((variant - 1) / 3) % 4) * 0.04f;
            synth_params.repetition_penalty = 1.15f;
            synth_params.max_new_frames = 250;

            struct vieneu_audio audio = {0};
            auto s0 = std::chrono::high_resolution_clock::now();
            int ret = vieneu_synthesize_v3(ctx, &synth_params, &audio);
            auto s1 = std::chrono::high_resolution_clock::now();

            if (ret != 0) {
                fprintf(stderr, "\nFailed to synthesize phrase %s variant %d: %s\n",
                    phrase.id.c_str(), variant, vieneu_last_error());
                vieneu_free(ctx);
                return 1;
            }

            double synthMs = std::chrono::duration<double, std::milli>(s1 - s0).count();

            // Convert to 16-bit PCM with 1.25x gain boost
            std::vector<int16_t> pcm(audio.n_samples);
            constexpr float kGainBoost = 1.25f;
            for (int i = 0; i < audio.n_samples; ++i) {
                float s = audio.samples[i] * kGainBoost;
                if (s > 1.0f) s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                pcm[i] = static_cast<int16_t>(s * 32767.0f);
            }

            writeWavFile(targetWav, pcm.data(), audio.n_samples, audio.sample_rate);
            writeWavFile(buildWav, pcm.data(), audio.n_samples, audio.sample_rate);

            vieneu_audio_free(&audio);

            ++completed;
            printf("[%zu/%zu] %s variant %02d (%.0fms)\n",
                completed, totalFiles, phrase.id.c_str(), variant, synthMs);
        }
    }

    auto genEnd = std::chrono::high_resolution_clock::now();
    double totalSec = std::chrono::duration<double>(genEnd - genStart).count();
    printf("\nAll %zu WAV files generated successfully in %.2f seconds (avg %.1f ms/file)\n",
        completed, totalSec, (totalSec * 1000.0) / completed);

    // Generate manifest.json
    std::ofstream manifest(outputDir / "manifest.json");
    manifest << "{\n";
    manifest << "  \"version\": 2,\n";
    manifest << "  \"backend\": \"VieNeu-TTS\",\n";
    manifest << "  \"voice\": \"Minh Quân\",\n";
    manifest << "  \"fine_tuned\": true,\n";
    manifest << "  \"variants_per_phrase\": " << kVariants << ",\n";
    manifest << "  \"entries\": [\n";

    for (size_t i = 0; i < phrases.size(); ++i) {
        manifest << "    {\n";
        manifest << "      \"id\": \"" << phrases[i].id << "\",\n";
        manifest << "      \"text\": \"" << phrases[i].text << "\",\n";
        manifest << "      \"files\": [\n";
        for (int v = 1; v <= kVariants; ++v) {
            char fbuf[32];
            snprintf(fbuf, sizeof(fbuf), "\"%s/%02d.wav\"", phrases[i].id.c_str(), v);
            manifest << "        " << fbuf << (v < kVariants ? "," : "") << "\n";
        }
        manifest << "      ]\n";
        manifest << "    }" << (i + 1 < phrases.size() ? "," : "") << "\n";
    }

    manifest << "  ]\n";
    manifest << "}\n";
    manifest.close();

    // Copy manifest to build directory
    fs::copy_file(outputDir / "manifest.json", buildOutputDir / "manifest.json",
        fs::copy_options::overwrite_existing);

    printf("Updated manifest.json in assets/spotter and build/audio/spotter\n");

    vieneu_free(ctx);
    return 0;
}
