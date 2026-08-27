#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "c-api.h"

// 简易 WAV 读取：仅支持 PCM 16bit 单声道/双声道
typedef struct { float *samples; int32_t sample_rate; int32_t num_samples; } Wave;

static Wave read_wav(const char *filename) {
    Wave w = {0};
    FILE *f = fopen(filename, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", filename); return w; }
    char id[5] = {0};
    uint32_t res;
    uint16_t audio_fmt, channels, bits;
    uint32_t sample_rate, byte_rate, data_size = 0;
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) != 0) { fclose(f); return w; }
    fseek(f, 4, SEEK_CUR);
    fread(id, 1, 4, f);
    if (memcmp(id, "WAVE", 4) != 0) { fclose(f); return w; }
    // 遍历 chunk
    while (fread(id, 1, 4, f) == 4) {
        fread(&res, 4, 1, f);
        if (memcmp(id, "fmt ", 4) == 0) {
            fread(&audio_fmt, 2, 1, f); fread(&channels, 2, 1, f);
            fread(&sample_rate, 4, 1, f); fread(&byte_rate, 4, 1, f);
            fseek(f, 6, SEEK_CUR); fread(&bits, 2, 1, f);
            if (res > 16) fseek(f, res - 16, SEEK_CUR);
        } else if (memcmp(id, "data", 4) == 0) {
            data_size = res;
            break;
        } else {
            fseek(f, res, SEEK_CUR);
        }
    }
    int16_t *pcm = (int16_t*)malloc(data_size);
    if (fread(pcm, 1, data_size, f) != data_size) { /* short */ }
    fclose(f);
    int32_t n = data_size / 2;
    if (channels == 2) n /= 2;
    w.samples = (float*)malloc(sizeof(float)*n);
    for (int32_t i = 0; i < n; i++) w.samples[i] = pcm[i*channels] / 32768.0f;
    w.sample_rate = sample_rate;
    w.num_samples = n;
    free(pcm);
    return w;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <wav>\n", argv[0]); return 1; }
    const char *wav_path = argv[1];
    const char *model_path = getenv("SENSEVOICE_MODEL");
    const char *tokens_path = getenv("SENSEVOICE_TOKENS");

    if (!model_path || !tokens_path) {
        fprintf(stderr, "set env SENSEVOICE_MODEL and SENSEVOICE_TOKENS\n");
        return 1;
    }

    SherpaOnnxOfflineModelConfig mc;
    memset(&mc, 0, sizeof(mc));
    mc.sense_voice = (SherpaOnnxOfflineSenseVoiceModelConfig){
        .model = model_path,
        .language = "zh",
        .use_itn = 1,
    };
    mc.tokens = tokens_path;
    mc.num_threads = 4;
    mc.debug = 0;
    mc.provider = "cpu";

    SherpaOnnxFeatureConfig fc;
    memset(&fc, 0, sizeof(fc));
    fc.sample_rate = 16000;
    fc.feature_dim = 80;

    SherpaOnnxOfflineRecognizerConfig rc;
    memset(&rc, 0, sizeof(rc));
    rc.feat_config = fc;
    rc.model_config = mc;
    rc.decoding_method = "greedy_search";

    const SherpaOnnxOfflineRecognizer *rec =
        SherpaOnnxCreateOfflineRecognizer(&rc);
    if (!rec) { fprintf(stderr, "failed to create recognizer\n"); return 1; }

    Wave w = read_wav(wav_path);
    if (!w.samples) { fprintf(stderr, "failed to read wav\n"); return 1; }

    SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(rec);
    SherpaOnnxAcceptWaveformOffline(stream, w.sample_rate, w.samples, w.num_samples);
    SherpaOnnxDecodeOfflineStream(rec, stream);
    const SherpaOnnxOfflineRecognizerResult *r = SherpaOnnxGetOfflineStreamResult(stream);
    printf("%s\n", r->text);
    fflush(stdout);

    SherpaOnnxDestroyOfflineStream(stream);
    SherpaOnnxDestroyOfflineRecognizer(rec);
    free(w.samples);
    return 0;
}
