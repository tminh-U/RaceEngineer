#include "v3_native_backbone_llama.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>

#include "ggml-backend.h"

bool V3NativeBackbone::backend_initialized_ = false;

namespace {

int env_int_or_default(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return (std::max)(1, fallback);
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed <= 0) {
        return (std::max)(1, fallback);
    }
    return static_cast<int>((std::min)(parsed, 256L));
}

} // namespace

V3NativeBackbone::V3NativeBackbone() {}

V3NativeBackbone::~V3NativeBackbone() {
    free_resources();
}

bool V3NativeBackbone::initialize(const V3BackboneParams& params) {
    free_resources();

    if (!backend_initialized_) {
        // Quiet logging
        llama_log_set([](enum ggml_log_level level, const char * text, void * user_data) {
            (void)user_data;
            if (level >= GGML_LOG_LEVEL_ERROR && text) {
                fputs(text, stderr);
            }
        }, nullptr);
        llama_backend_init();
        backend_initialized_ = true;
    }

    llama_model_params model_params = llama_model_default_params();
    int gpu_layers = params.n_gpu_layers;
    const char* env_layers = std::getenv("VIENEU_GPU_LAYERS");
    if (env_layers && *env_layers) {
        gpu_layers = std::atoi(env_layers);
    }
    model_params.n_gpu_layers = gpu_layers;

    std::vector<ggml_backend_dev_t> dev_ptrs;
    if (gpu_layers > 0) {
        size_t n_devs = ggml_backend_dev_count();
        ggml_backend_dev_t selected_gpu = nullptr;

        const char* env_dev = std::getenv("VIENEU_GPU_DEVICE");
        std::string filter = env_dev ? env_dev : params.device_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        for (size_t i = 0; i < n_devs; ++i) {
            ggml_backend_dev_t d = ggml_backend_dev_get(i);
            const char* d_name = ggml_backend_dev_name(d);
            const char* d_desc = ggml_backend_dev_description(d);
            enum ggml_backend_dev_type d_type = ggml_backend_dev_type(d);
            if (d_type == GGML_BACKEND_DEVICE_TYPE_CPU) continue;

            std::string name_s = d_name ? d_name : "";
            std::transform(name_s.begin(), name_s.end(), name_s.begin(), ::tolower);
            std::string desc_s = d_desc ? d_desc : "";
            std::transform(desc_s.begin(), desc_s.end(), desc_s.begin(), ::tolower);

            bool matches = false;
            if (!filter.empty()) {
                if (filter == "igpu" && d_type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
                    matches = true;
                } else if (name_s.find(filter) != std::string::npos || desc_s.find(filter) != std::string::npos) {
                    matches = true;
                } else if (filter == "680m" && (desc_s.find("graphics") != std::string::npos && d_type == GGML_BACKEND_DEVICE_TYPE_IGPU)) {
                    matches = true;
                }
            } else {
                if (d_type == GGML_BACKEND_DEVICE_TYPE_IGPU) matches = true;
            }

            if (matches) {
                selected_gpu = d;
                break;
            }
        }

        // Fallback: pick first non-CPU device if filter didn't match
        if (!selected_gpu && n_devs > 0) {
            for (size_t i = 0; i < n_devs; ++i) {
                ggml_backend_dev_t d = ggml_backend_dev_get(i);
                if (ggml_backend_dev_type(d) != GGML_BACKEND_DEVICE_TYPE_CPU) {
                    selected_gpu = d;
                    break;
                }
            }
        }

        if (selected_gpu) {
            dev_ptrs.push_back(selected_gpu);
            dev_ptrs.push_back(nullptr);
            model_params.devices = dev_ptrs.data();
            std::cout << "[V3NativeBackbone] Offloading " << gpu_layers << " layers to GPU device: "
                      << (ggml_backend_dev_name(selected_gpu) ? ggml_backend_dev_name(selected_gpu) : "") << " ("
                      << (ggml_backend_dev_description(selected_gpu) ? ggml_backend_dev_description(selected_gpu) : "") << ")\n";
        } else {
            std::cout << "[V3NativeBackbone] No non-CPU device found; running backbone on CPU.\n";
            model_params.n_gpu_layers = 0;
        }
    }

    model_ = llama_model_load_from_file(params.model_path.c_str(), model_params);
    if (!model_) {
        std::cerr << "[V3NativeBackbone] Failed to load model from " << params.model_path << std::endl;
        return false;
    }

    hidden_size_ = llama_model_n_embd(model_);
    const int n_threads = env_int_or_default("VIENEU_BACKBONE_THREADS", params.n_threads);
    const int n_threads_batch = env_int_or_default("VIENEU_BACKBONE_BATCH_THREADS", params.n_threads_batch > 0 ? params.n_threads_batch : n_threads);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_threads = n_threads;
    ctx_params.n_threads_batch = n_threads_batch;
    ctx_params.embeddings = true; // Enable embeddings extraction
    ctx_params.no_perf = true;

    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        std::cerr << "[V3NativeBackbone] Failed to initialize context." << std::endl;
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    prefill_capacity_ = ctx_params.n_ctx;
    prefill_batch_ = llama_batch_init(prefill_capacity_, hidden_size_, 1);
    prefill_batch_initialized_ = true;
    decode_batch_ = llama_batch_init(1, hidden_size_, 1);
    decode_batch_initialized_ = true;
    decoded_pos_ = 0;
    return true;
}

void V3NativeBackbone::free_resources() {
    if (prefill_batch_initialized_) {
        llama_batch_free(prefill_batch_);
        prefill_batch_ = {};
        prefill_batch_initialized_ = false;
    }
    if (decode_batch_initialized_) {
        llama_batch_free(decode_batch_);
        decode_batch_ = {};
        decode_batch_initialized_ = false;
    }
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    decoded_pos_ = 0;
    prefill_capacity_ = 0;
}

bool V3NativeBackbone::prefill(const std::vector<float>& embeds, std::vector<float>& out_hidden) {
    if (!ctx_ || embeds.empty()) return false;

    const int32_t n_tokens = static_cast<int32_t>(embeds.size() / hidden_size_);
    if (n_tokens <= 0) return false;
    if (!prefill_batch_initialized_ || n_tokens > prefill_capacity_) {
        std::cerr << "[V3NativeBackbone] Prefill batch capacity is insufficient." << std::endl;
        return false;
    }

    // Reset KV cache and decoded position
    clear_kv_cache();

    // Copy input embeddings
    std::memcpy(prefill_batch_.embd, embeds.data(), embeds.size() * sizeof(float));

    for (int32_t i = 0; i < n_tokens; ++i) {
        prefill_batch_.pos[i] = i;
        prefill_batch_.n_seq_id[i] = 1;
        prefill_batch_.seq_id[i][0] = 0;
        prefill_batch_.logits[i] = (i == n_tokens - 1); // request logits/embedding output for last token only
    }
    prefill_batch_.n_tokens = n_tokens;

    int res = llama_decode(ctx_, prefill_batch_);

    if (res != 0) {
        std::cerr << "[V3NativeBackbone] Prefill failed with code: " << res << std::endl;
        return false;
    }

    decoded_pos_ = n_tokens;

    // Retrieve last hidden state from context output embeddings
    float* emb_ptr = llama_get_embeddings_ith(ctx_, -1);
    if (!emb_ptr) {
        // Fallback to first if -1 is null
        emb_ptr = llama_get_embeddings_ith(ctx_, 0);
    }
    if (!emb_ptr) {
        std::cerr << "[V3NativeBackbone] Prefill failed to retrieve output embeddings." << std::endl;
        return false;
    }

    out_hidden.resize(static_cast<size_t>(hidden_size_));
    std::memcpy(out_hidden.data(), emb_ptr, hidden_size_ * sizeof(float));
    return true;
}

bool V3NativeBackbone::decode_step(const std::vector<float>& slot_embed, std::vector<float>& out_hidden) {
    if (!ctx_ || !decode_batch_initialized_ || slot_embed.size() != static_cast<size_t>(hidden_size_)) return false;

    std::memcpy(decode_batch_.embd, slot_embed.data(), slot_embed.size() * sizeof(float));

    decode_batch_.pos[0] = decoded_pos_;
    decode_batch_.n_seq_id[0] = 1;
    decode_batch_.seq_id[0][0] = 0;
    decode_batch_.logits[0] = true;
    decode_batch_.n_tokens = 1;

    int res = llama_decode(ctx_, decode_batch_);

    if (res != 0) {
        std::cerr << "[V3NativeBackbone] Decode step failed with code: " << res << std::endl;
        return false;
    }

    decoded_pos_ += 1;

    float* emb_ptr = llama_get_embeddings_ith(ctx_, 0);
    if (!emb_ptr) {
        std::cerr << "[V3NativeBackbone] Decode step failed to retrieve output embeddings." << std::endl;
        return false;
    }

    out_hidden.resize(static_cast<size_t>(hidden_size_));
    std::memcpy(out_hidden.data(), emb_ptr, hidden_size_ * sizeof(float));
    return true;
}

void V3NativeBackbone::clear_kv_cache() {
    if (ctx_) {
        llama_memory_t mem = llama_get_memory(ctx_);
        if (mem) {
            llama_memory_clear(mem, true);
        }
    }
    decoded_pos_ = 0;
}
