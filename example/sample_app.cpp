// sample_app.cpp
//
// NukedEngineApi の使用例。
// YMEngine の main.cpp と同じ API のみ使用し、互換性を示す。

#include "NukedEngineApi.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>

// ------------------------------------------------------------
// 簡易 WAV ファイル書き出しヘルパー
// ------------------------------------------------------------
static void write_wav(const char* path, const float* l, const float* r,
                      uint32_t frames, uint32_t sr)
{
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return; }

    uint32_t data_bytes = frames * 2 * sizeof(int16_t);
    uint32_t riff_size  = 36 + data_bytes;
    uint16_t ch = 2, bits = 16;
    uint32_t byte_rate = sr * ch * bits / 8;
    uint16_t block_align = static_cast<uint16_t>(ch * bits / 8);

    // RIFF ヘッダ
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    // fmt チャンク
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    uint16_t pcm = 1;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    // data チャンク
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    for (uint32_t i = 0; i < frames; ++i) {
        auto to16 = [](float v) -> int16_t {
            int32_t x = static_cast<int32_t>(v * 32767.0f);
            if (x >  32767) x =  32767;
            if (x < -32768) x = -32768;
            return static_cast<int16_t>(x);
        };
        int16_t s[2] = { to16(l[i]), to16(r[i]) };
        fwrite(s, 2, 2, f);
    }
    fclose(f);
    printf("  Wrote: %s  (%u frames @ %u Hz)\n", path, frames, sr);
}

// ------------------------------------------------------------
// OPL3 デモ – YMEngineのmain.cppと同形のコード
// ------------------------------------------------------------
static void demo_opl3(FmEngineHandle eng)
{
    uint32_t id;
    if (FmEngine_AddChip(eng, FM_CHIP_OPL3, 0, &id) != FM_OK) {
        fprintf(stderr, "[OPL3] AddChip failed\n"); return;
    }
    printf("[OPL3] %s  native=%u Hz\n",
           FmEngine_GetChipName(eng, id),
           FmEngine_GetNativeRate(eng, id));
    FmEngine_SetGain(eng, id, 0.7f, 0.7f);

    // OPL3 モード有効化 (port=1 でバンク 1 アドレス空間へ)
    FmEngine_Write(eng, id, 0x05, 0x01, 1);

    // スロット 0 / チャンネル 0 のオペレーター設定
    FmEngine_Write(eng, id, 0x20, 0x01, 0); // Modulator AM/VIB/EG/KSR/MULT
    FmEngine_Write(eng, id, 0x40, 0x10, 0); // Modulator KSL/TL
    FmEngine_Write(eng, id, 0x60, 0xF0, 0); // Modulator AR/DR
    FmEngine_Write(eng, id, 0x80, 0x77, 0); // Modulator SL/RR
    FmEngine_Write(eng, id, 0x23, 0x01, 0); // Carrier
    FmEngine_Write(eng, id, 0x43, 0x00, 0);
    FmEngine_Write(eng, id, 0x63, 0xF0, 0);
    FmEngine_Write(eng, id, 0x83, 0x77, 0);

    // チャンネル 0 F-num (440 Hz 相当) + Key-On
    FmEngine_Write(eng, id, 0xA0, 0x49, 0);
    FmEngine_Write(eng, id, 0xB0, 0x32, 0); // Key-On
    FmEngine_Write(eng, id, 0xC0, 0x31, 0); // stereo
}

// ------------------------------------------------------------
// OPN2 デモ
// ------------------------------------------------------------
static void demo_opn2(FmEngineHandle eng)
{
    uint32_t id;
    if (FmEngine_AddChip(eng, FM_CHIP_OPN2, 0, &id) != FM_OK) {
        fprintf(stderr, "[OPN2] AddChip failed\n"); return;
    }
    printf("[OPN2] %s  native=%u Hz\n",
           FmEngine_GetChipName(eng, id),
           FmEngine_GetNativeRate(eng, id));
    FmEngine_SetGain(eng, id, 0.7f, 0.7f);

    // CH1: アルゴリズム 7, FB=0 (port=0:addr, port=1:data)
    FmEngine_Write(eng, id, 0xB0, 0x07, 0); // addr
    FmEngine_Write(eng, id, 0x00, 0x07, 1); // data: ALG=7
    // OP1 TL=0
    FmEngine_Write(eng, id, 0x40, 0x00, 0);
    FmEngine_Write(eng, id, 0x00, 0x00, 1);
    // Key-On CH0 全 OP
    FmEngine_Write(eng, id, 0x28, 0xF0, 0);
    FmEngine_Write(eng, id, 0x00, 0xF0, 1);
}

// ------------------------------------------------------------
// OPM デモ
// ------------------------------------------------------------
static void demo_opm(FmEngineHandle eng)
{
    uint32_t id;
    if (FmEngine_AddChip(eng, FM_CHIP_OPM, 0, &id) != FM_OK) {
        fprintf(stderr, "[OPM] AddChip failed\n"); return;
    }
    printf("[OPM]  %s  native=%u Hz\n",
           FmEngine_GetChipName(eng, id),
           FmEngine_GetNativeRate(eng, id));
    FmEngine_SetGain(eng, id, 0.6f, 0.6f);

    // CH0 RL/FB/CON (port=0:addr, port=1:data)
    FmEngine_Write(eng, id, 0x20, 0xC0, 0);
    FmEngine_Write(eng, id, 0x20, 0xC0, 1);
    // Key-On 全 OP
    FmEngine_Write(eng, id, 0x08, 0x78, 0);
    FmEngine_Write(eng, id, 0x08, 0x78, 1);
}

// ------------------------------------------------------------
// OPLL デモ (拡張チップ)
// ------------------------------------------------------------
static void demo_opll(FmEngineHandle eng)
{
    uint32_t id;
    if (FmEngine_AddChip(eng, FM_CHIP_OPLL, 0, &id) != FM_OK) {
        fprintf(stderr, "[OPLL] AddChip failed\n"); return;
    }
    printf("[OPLL] %s  native=%u Hz\n",
           FmEngine_GetChipName(eng, id),
           FmEngine_GetNativeRate(eng, id));
    FmEngine_SetGain(eng, id, 0.6f, 0.6f);

    // CH0 F-num + Key-On
    FmEngine_Write(eng, id, 0x10, 0xC2, 0);
    FmEngine_Write(eng, id, 0xC2, 0x00, 1);
    FmEngine_Write(eng, id, 0x20, 0x15, 0);
    FmEngine_Write(eng, id, 0x15, 0x00, 1);
    FmEngine_Write(eng, id, 0x30, 0x00, 0);
    FmEngine_Write(eng, id, 0x00, 0x00, 1);
}

// ------------------------------------------------------------
// PSG デモ (拡張チップ)
// ------------------------------------------------------------
static void demo_psg(FmEngineHandle eng)
{
    uint32_t id;
    if (FmEngine_AddChip(eng, FM_CHIP_PSG, 0, &id) != FM_OK) {
        fprintf(stderr, "[PSG] AddChip failed\n"); return;
    }
    printf("[PSG]  %s  native=%u Hz\n",
           FmEngine_GetChipName(eng, id),
           FmEngine_GetNativeRate(eng, id));
    FmEngine_SetGain(eng, id, 0.5f, 0.5f);

    // CH0 トーン ~440 Hz
    FmEngine_Write(eng, id, 0x00, 0x8F, 0); // latch CH0 tone, low nibble
    FmEngine_Write(eng, id, 0x00, 0x07, 0); // high bits
    FmEngine_Write(eng, id, 0x00, 0x90, 0); // CH0 volume max
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main()
{
    const uint32_t SAMPLE_RATE = 48000;
    const uint32_t DURATION    = 2;   // 秒
    const uint32_t FRAMES      = SAMPLE_RATE * DURATION;
    const uint32_t BLOCK       = 512;

    printf("NukedEngine sample_app (YMEngine-compatible API)\n");
    printf("Sample rate: %u Hz,  Duration: %u s\n\n", SAMPLE_RATE, DURATION);

    // --- エンジン作成 ---
    FmEngineHandle eng = FmEngine_Create(SAMPLE_RATE);
    if (!eng) { fprintf(stderr, "FmEngine_Create failed\n"); return 1; }

    // --- チップ追加 ---
    demo_opl3(eng);
    demo_opn2(eng);
    demo_opm(eng);
    demo_opll(eng);
    demo_psg(eng);

    // --- 音声生成 → WAV 書き出し ---
    std::vector<float> out_l(FRAMES), out_r(FRAMES);
    uint32_t written = 0;
    while (written < FRAMES) {
        uint32_t n = std::min(BLOCK, FRAMES - written);
        FmEngine_Generate(eng, out_l.data() + written, out_r.data() + written, n);
        written += n;
    }

    // クリッピング防止: 全チップ合算後にソフトリミット
    for (uint32_t i = 0; i < FRAMES; ++i) {
        auto soft = [](float v) {
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            return v;
        };
        out_l[i] = soft(out_l[i]);
        out_r[i] = soft(out_r[i]);
    }

    write_wav("nuked_output.wav", out_l.data(), out_r.data(), FRAMES, SAMPLE_RATE);

    // --- ゲイン確認 ---
    float gl, gr;
    uint32_t chip0_id = 0;
    FmEngine_GetGain(eng, chip0_id, &gl, &gr);
    printf("[Info] chip#0 gain L=%.2f R=%.2f\n", gl, gr);
    printf("[Info] engine sample_rate=%u\n", FmEngine_GetSampleRate(eng));

    // --- 後始末 ---
    FmEngine_Destroy(eng);
    printf("\nDone.\n");
    return 0;
}
