#include "vieneu/vieneu_tts.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

struct TestCase {
    const char* category;
    const char* text;
    int max_frames;
};

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    printf("=================================================================\n");
    printf("     VieNeu-TTS.cpp Benchmark (AMD Radeon 680M Vulkan Offload)   \n");
    printf("=================================================================\n");
    printf("VieNeu-TTS engine version: %s\n\n", vieneu_version());

    const char* model_dir = "models\\vieneu-v3";
    const char* codec_dir = "models\\vieneu-v3\\codec";
    const char* voice_id = "Minh Quân";

    // 1. Cold Initialization
    printf("[1/3] Đo thời gian khởi tạo Cold Load (Vulkan + Qwen3 Backbone + Tokenizer + Codec ONNX):\n");
    struct vieneu_init_params_v2 init_params;
    vieneu_init_v2_default_params(&init_params);
    init_params.profile = "vieneu-v3-native";
    init_params.model_dir = model_dir;
    init_params.codec_dir = codec_dir;
    init_params.n_threads = 4;

    auto t_init_start = std::chrono::high_resolution_clock::now();
    struct vieneu_context* ctx = vieneu_init_v2(&init_params);
    auto t_init_end = std::chrono::high_resolution_clock::now();

    if (!ctx) {
        fprintf(stderr, "Lỗi khởi tạo: %s\n", vieneu_last_error());
        return 1;
    }
    double init_ms = std::chrono::duration<double, std::milli>(t_init_end - t_init_start).count();
    printf("  --> Thời gian khởi tạo hoàn tất: %.2f ms (%.2f s)\n\n", init_ms, init_ms / 1000.0);

    // 2. Warm-up
    printf("[2/3] Thực hiện Warm-up lần đầu (Shaders JIT & GPU pipeline cache):\n");
    struct vieneu_tts_params_v3 warmup_params;
    vieneu_tts_v3_default_params(&warmup_params);
    warmup_params.text = "Xin chào.";
    warmup_params.voice_id = voice_id;
    warmup_params.ref_audio_path = nullptr;
    warmup_params.use_ref_codes = true;
    warmup_params.denoise_ref = false;
    warmup_params.max_new_frames = 50;

    struct vieneu_audio warmup_audio = {0};
    auto t_warmup_start = std::chrono::high_resolution_clock::now();
    int ret_w = vieneu_synthesize_v3(ctx, &warmup_params, &warmup_audio);
    auto t_warmup_end = std::chrono::high_resolution_clock::now();
    if (ret_w == 0) {
        double warmup_ms = std::chrono::duration<double, std::milli>(t_warmup_end - t_warmup_start).count();
        double warmup_dur = (double)warmup_audio.n_samples / (double)warmup_audio.sample_rate;
        printf("  --> Warm-up thành công trong %.2f ms (Audio: %.2f s, RTF: %.3f)\n\n",
               warmup_ms, warmup_dur, (warmup_ms / 1000.0) / warmup_dur);
        vieneu_audio_free(&warmup_audio);
    } else {
        printf("  --> Warm-up cảnh báo: %s\n\n", vieneu_last_error());
    }

    // 3. Test scenarios
    printf("[3/3] Benchmark các kịch bản thực tế (3 lần chạy mỗi câu, đo độ trễ ms & RTF):\n");

    std::vector<TestCase> cases = {
        {
            "Ngắn (Lệnh Pit/Cờ khẩn cấp)",
            "Box box, vào pit vòng này.",
            80
        },
        {
            "Trung bình (Báo cáo lốp & động cơ)",
            "Nhiệt độ lốp trước bên phải đang là 102 độ C, hãy giảm ép ga ở cua 4.",
            150
        },
        {
            "Dài (Phân tích chiến thuật & khoảng cách)",
            "Khoảng cách với xe P2 phía sau đang là 3 giây 2, lốp sau mòn 15 phần trăm, nhiên liệu còn đủ chạy 8 vòng nữa, tiếp tục duy trì pace này.",
            250
        }
    };

    printf("---------------------------------------------------------------------------------------------------\n");
    printf("%-38s | %-10s | %-12s | %-10s | %-10s | %-8s\n",
           "Kịch bản", "Độ dài âm", "Thời gian TB", "Nhanh nhất", "Chậm nhất", "RTF");
    printf("---------------------------------------------------------------------------------------------------\n");

    for (const auto& tc : cases) {
        std::vector<double> latencies;
        double audio_dur = 0.0;

        for (int run = 0; run < 3; ++run) {
            struct vieneu_tts_params_v3 p;
            vieneu_tts_v3_default_params(&p);
            p.text = tc.text;
            p.voice_id = voice_id;
            p.ref_audio_path = nullptr;
            p.use_ref_codes = true;
            p.denoise_ref = false;
            p.max_new_frames = tc.max_frames;

            struct vieneu_audio audio = {0};
            auto s0 = std::chrono::high_resolution_clock::now();
            int ret = vieneu_synthesize_v3(ctx, &p, &audio);
            auto s1 = std::chrono::high_resolution_clock::now();

            if (ret == 0) {
                double ms = std::chrono::duration<double, std::milli>(s1 - s0).count();
                latencies.push_back(ms);
                audio_dur = (double)audio.n_samples / (double)audio.sample_rate;
                vieneu_audio_free(&audio);
            } else {
                fprintf(stderr, "Synthesis failed for '%s': %s\n", tc.text, vieneu_last_error());
            }
        }

        if (!latencies.empty()) {
            double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
            double avg = sum / latencies.size();
            double min_l = *std::min_element(latencies.begin(), latencies.end());
            double max_l = *std::max_element(latencies.begin(), latencies.end());
            double rtf = (avg / 1000.0) / (audio_dur > 0.0 ? audio_dur : 1.0);

            char dur_str[32], avg_str[32], min_str[32], max_str[32], rtf_str[32];
            snprintf(dur_str, sizeof(dur_str), "%.2f s", audio_dur);
            snprintf(avg_str, sizeof(avg_str), "%.1f ms", avg);
            snprintf(min_str, sizeof(min_str), "%.1f ms", min_l);
            snprintf(max_str, sizeof(max_str), "%.1f ms", max_l);
            snprintf(rtf_str, sizeof(rtf_str), "%.3f", rtf);

            printf("%-38s | %-10s | %-12s | %-10s | %-10s | %-8s\n",
                   tc.category, dur_str, avg_str, min_str, max_str, rtf_str);
        }
    }

    printf("---------------------------------------------------------------------------------------------------\n");
    printf("Ghi chú RTF (Real-Time Factor): RTF < 1.0 nghĩa là tốc độ tạo giọng nói nhanh hơn tốc độ nói thực tế.\n");
    printf("Ví dụ RTF = 0.5 nghĩa là đoạn âm thanh 2 giây được tạo ra chỉ trong 1 giây (2x real-time speed).\n");
    printf("=================================================================\n");

    vieneu_free(ctx);
    return 0;
}
