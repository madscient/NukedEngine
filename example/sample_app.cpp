// sample_app.cpp
//
// NukedEngineApi の使用例。
// YMEngine の main.cpp と同じ API のみ使用し、互換性を示す。
// WASAPI でリアルタイム再生する。Enter キーで終了。

#include "NukedEngineApi.h"
#include <cstdio>

// ------------------------------------------------------------
// OPL3 デモ
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

    // OPL3 モード有効化
    FmEngine_Write(eng, id, 0x05, 0x01, 1);

    // CH0 オペレーター設定
    FmEngine_Write(eng, id, 0x20, 0x01, 0); // Modulator AM/VIB/EG/KSR/MULT
    FmEngine_Write(eng, id, 0x40, 0x10, 0); // Modulator KSL/TL
    FmEngine_Write(eng, id, 0x60, 0xF0, 0); // Modulator AR/DR
    FmEngine_Write(eng, id, 0x80, 0x77, 0); // Modulator SL/RR
    FmEngine_Write(eng, id, 0x23, 0x01, 0); // Carrier
    FmEngine_Write(eng, id, 0x43, 0x00, 0);
    FmEngine_Write(eng, id, 0x63, 0xF0, 0);
    FmEngine_Write(eng, id, 0x83, 0x77, 0);

    // CH0 F-num (440 Hz 相当) + Key-On
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

    // CH0: ALG=7, TL=0, Key-On 全 OP
    FmEngine_Write(eng, id, 0xB0, 0x07, 0);
    FmEngine_Write(eng, id, 0x00, 0x07, 1);
    FmEngine_Write(eng, id, 0x40, 0x00, 0);
    FmEngine_Write(eng, id, 0x00, 0x00, 1);
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

    // CH0 RL/FB/CON, Key-On 全 OP
    FmEngine_Write(eng, id, 0x20, 0xC0, 0);
    FmEngine_Write(eng, id, 0x20, 0xC0, 1);
    FmEngine_Write(eng, id, 0x08, 0x78, 0);
    FmEngine_Write(eng, id, 0x08, 0x78, 1);
}

// ------------------------------------------------------------
// OPLL デモ
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
// PSG デモ (FM_CHIP_EXT_DCSG → Nuked-PSG / YM7101)
// ------------------------------------------------------------
static void demo_psg(FmEngineHandle eng)
{
    uint32_t id;
    if (FmEngine_AddExtChip(eng, FM_CHIP_EXT_DCSG, 0, &id) != FM_OK) {
        fprintf(stderr, "[PSG] AddExtChip failed\n"); return;
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
    printf("NukedEngine sample_app (YMEngine-compatible API)\n\n");

    // --- エンジン作成 (WASAPI がデバイスのサンプルレートで動作するため 0 で作成後、
    //     Wasapi_Create → Wasapi_GetSampleRate で実際のレートを取得する。
    //     ここでは一般的な 48000 Hz を仮定して初期化する) ---
    const uint32_t SAMPLE_RATE = 48000;
    FmEngineHandle eng = FmEngine_Create(SAMPLE_RATE);
    if (!eng) { fprintf(stderr, "FmEngine_Create failed\n"); return 1; }

    // --- チップ追加・レジスタ設定 ---
    demo_opl3(eng);
    demo_opn2(eng);
    demo_opm(eng);
    demo_opll(eng);
    demo_psg(eng);

    printf("\nEngine sample rate : %u Hz\n", FmEngine_GetSampleRate(eng));

    // --- WASAPI 出力作成 (Shared mode) ---
    WasapiHandle wasapi = Wasapi_Create(eng, 0);
    if (!wasapi) {
        fprintf(stderr, "Wasapi_Create failed\n");
        FmEngine_Destroy(eng);
        return 1;
    }
    printf("WASAPI sample rate : %u Hz\n", Wasapi_GetSampleRate(wasapi));

    // --- 再生開始 ---
    FmResult hr = Wasapi_Start(wasapi);
    if (hr != FM_OK) {
        fprintf(stderr, "Wasapi_Start failed (%d)\n", hr);
        Wasapi_Destroy(wasapi);
        FmEngine_Destroy(eng);
        return 1;
    }

    printf("\nPlaying... Press Enter to stop.\n");
    getchar();

    // --- 停止・後始末 ---
    Wasapi_Stop(wasapi);
    Wasapi_Destroy(wasapi);
    FmEngine_Destroy(eng);

    printf("Done.\n");
    return 0;
}
