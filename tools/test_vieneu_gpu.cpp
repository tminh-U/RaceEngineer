#include "vieneu/vieneu_tts.h"
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

int main(int argc, char** argv) {
    SetEnvironmentVariableA("VIENEU_V3_NATIVE_DEBUG_TAGS", "1");
    _putenv("VIENEU_V3_NATIVE_DEBUG_TAGS=1");
    printf("=== Testing VieNeu-TTS.cpp with AMD Radeon 680M Vulkan Offload ===\n");
    printf("VieNeu version: %s\n", vieneu_version());

    struct vieneu_init_params_v2 init_params;
    vieneu_init_v2_default_params(&init_params);
    init_params.profile = "vieneu-v3-native";
    init_params.model_dir = "C:\\Users\\tminh\\Documents\\Projects\\RaceEngineer\\models\\vieneu-v3";
    init_params.codec_dir = "C:\\Users\\tminh\\Documents\\Projects\\RaceEngineer\\models\\vieneu-v3\\codec";
    init_params.n_threads = 4;

    printf("Initializing engine from %s...\n", init_params.model_dir);
    auto t0 = std::chrono::high_resolution_clock::now();
    struct vieneu_context* ctx = vieneu_init_v2(&init_params);
    auto t1 = std::chrono::high_resolution_clock::now();
    if (!ctx) {
        fprintf(stderr, "Failed to initialize: %s\n", vieneu_last_error());
        return 1;
    }
    double init_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("Engine initialized successfully in %.2f ms!\n", init_ms);

    std::string text_utf8 = u8"áp suất lốp hiện không có sẵn";
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(GetCommandLineW(), &numArgs);
    if (argList && numArgs > 1) {
        text_utf8.clear();
        for (int i = 1; i < numArgs; ++i) {
            int utf8_len = WideCharToMultiByte(CP_UTF8, 0, argList[i], -1, nullptr, 0, nullptr, nullptr);
            if (utf8_len > 0) {
                std::vector<char> buf(utf8_len);
                WideCharToMultiByte(CP_UTF8, 0, argList[i], -1, buf.data(), utf8_len, nullptr, nullptr);
                if (!text_utf8.empty()) text_utf8 += " ";
                text_utf8 += buf.data();
            }
        }
        LocalFree(argList);
    }
    struct vieneu_tts_params_v3 synth_params;
    vieneu_tts_v3_default_params(&synth_params);
    synth_params.text = text_utf8.c_str();
    synth_params.voice_id = "Minh Quân";
    synth_params.style = "tu_nhien";
    synth_params.ref_audio_path = nullptr;
    synth_params.use_ref_codes = true;
    synth_params.denoise_ref = false;
    synth_params.temperature = 0.40f;
    synth_params.top_p = 0.90f;
    synth_params.repetition_penalty = 1.15f;
    synth_params.max_new_frames = 250;

    printf("Synthesizing with preset voice: %s (style: %s, temp: %.2f)\n", synth_params.voice_id, synth_params.style, synth_params.temperature);
    struct vieneu_audio audio = {0};
    auto s0 = std::chrono::high_resolution_clock::now();
    int ret = vieneu_synthesize_v3(ctx, &synth_params, &audio);
    auto s1 = std::chrono::high_resolution_clock::now();
    if (ret != 0) {
        fprintf(stderr, "Synthesis failed: %s\n", vieneu_last_error());
        vieneu_free(ctx);
        return 1;
    }
    double synth_ms = std::chrono::duration<double, std::milli>(s1 - s0).count();
    double duration_sec = (double)audio.n_samples / (double)audio.sample_rate;
    double rtf = (synth_ms / 1000.0) / duration_sec;

    printf("Synthesis succeeded!\n");
    printf("  Audio samples: %d (%.2f seconds @ %d Hz)\n", audio.n_samples, duration_sec, audio.sample_rate);
    printf("  Total synthesis time: %.2f ms\n", synth_ms);
    printf("  Real-time factor (RTF): %.3f (lower is faster, < 1.0 is faster than real-time)\n", rtf);

    // Write output WAV file (16-bit PCM)
    FILE* fp = fopen("build/output_minhduc.wav", "wb");
    if (fp) {
        uint32_t data_bytes = audio.n_samples * sizeof(int16_t);
        uint32_t total_bytes = 36 + data_bytes;
        uint32_t sample_rate = audio.sample_rate;
        uint16_t channels = audio.channels;
        uint16_t bits_per_sample = 16;
        uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
        uint16_t block_align = channels * (bits_per_sample / 8);

        fwrite("RIFF", 1, 4, fp);
        fwrite(&total_bytes, 4, 1, fp);
        fwrite("WAVE", 1, 4, fp);
        fwrite("fmt ", 1, 4, fp);
        uint32_t subchunk1_size = 16;
        uint16_t audio_format = 1; // PCM
        fwrite(&subchunk1_size, 4, 1, fp);
        fwrite(&audio_format, 2, 1, fp);
        fwrite(&channels, 2, 1, fp);
        fwrite(&sample_rate, 4, 1, fp);
        fwrite(&byte_rate, 4, 1, fp);
        fwrite(&block_align, 2, 1, fp);
        fwrite(&bits_per_sample, 2, 1, fp);
        fwrite("data", 1, 4, fp);
        fwrite(&data_bytes, 4, 1, fp);

        for (int i = 0; i < audio.n_samples; ++i) {
            float f = audio.samples[i];
            if (f > 1.0f) f = 1.0f;
            if (f < -1.0f) f = -1.0f;
            int16_t s = (int16_t)(f * 32767.0f);
            fwrite(&s, 2, 1, fp);
        }
        fclose(fp);
        printf("Saved generated audio to build-vieneu-vk/output_minhquan.wav\n");
    }

    vieneu_audio_free(&audio);
    vieneu_free(ctx);
    printf("=== Test Completed Successfully ===\n");
    return 0;
}
