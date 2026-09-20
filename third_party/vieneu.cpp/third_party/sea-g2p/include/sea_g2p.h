#ifndef SEA_G2P_H
#define SEA_G2P_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SeaG2pContext SeaG2pContext;

SeaG2pContext * sea_g2p_create(const char * lang, const char * dict_path);
void sea_g2p_destroy(SeaG2pContext * ctx);

char * sea_g2p_normalize(const SeaG2pContext * ctx, const char * text, int punc_norm);
char * sea_g2p_phonemize(const SeaG2pContext * ctx, const char * normalized_text, int punc_norm);
char * sea_g2p_run(const SeaG2pContext * ctx, const char * text, int punc_norm);
char * sea_g2p_punc_norm(const char * text);

void sea_g2p_free_string(char * value);
char * sea_g2p_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
