#include "VoiceRuntimeStubs.h"
#include <whisper.h>
#include <vieneu/vieneu_tts.h>

struct whisper_context {};
struct vieneu_context {};

extern "C" {
whisper_context_params whisper_context_default_params() { return {}; }
whisper_full_params whisper_full_default_params(whisper_sampling_strategy) { return {}; }
whisper_context* whisper_init_from_file_with_params(const char*, whisper_context_params)
{
    ++voice_test::sttLoads;
    return new whisper_context;
}
void whisper_free(whisper_context* context) { ++voice_test::sttFrees; delete context; }
int whisper_full(whisper_context*, whisper_full_params params, const float*, int)
{
    voice_test::sttThreads = params.n_threads;
    return 0;
}
int whisper_full_n_segments(whisper_context*) { return 1; }
const char* whisper_full_get_segment_text(whisper_context*, int) { return "Radio test"; }
int whisper_full_lang_id(whisper_context*) { return 0; }
const char* whisper_lang_str(int) { return "vi"; }
whisper_timings* whisper_get_timings(whisper_context*) { return new whisper_timings{}; }
void whisper_reset_timings(whisper_context*) {}
const char* whisper_print_system_info() { return "Lifecycle test stub; no inference"; }

void vieneu_init_v2_default_params(vieneu_init_params_v2* params) { *params = {}; }
vieneu_context* vieneu_init_v2(const vieneu_init_params_v2* params)
{
    ++voice_test::ttsLoads;
    voice_test::ttsThreads = params->n_threads;
    if (voice_test::failTtsInit) return nullptr;
    return new vieneu_context;
}
void vieneu_free(vieneu_context* context) { ++voice_test::ttsFrees; delete context; }
void vieneu_tts_v3_default_params(vieneu_tts_params_v3* params) { *params = {}; }
int vieneu_synthesize_v3(vieneu_context*, const vieneu_tts_params_v3*, vieneu_audio*)
{
    ++voice_test::ttsSyntheses;
    return -1; // No audio/device required: failed speech must also permit idle cleanup.
}
const char* vieneu_last_error() { return "Lifecycle test: synthetic synthesis failure"; }
void vieneu_audio_free(vieneu_audio*) {}
}
