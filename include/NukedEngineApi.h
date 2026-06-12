#pragma once

// NukedEngineApi.h
//
// YMEngine (madscient/YMEngine) の FmEngineApi.h と互換の C ファサード API。
// バックエンドを ymfm から Nuked シリーズエミュレーターに差し替えた実装。
//
// 互換性の方針:
//   - 関数シグネチャ・エラーコード・ハンドル型は FmEngineApi.h と完全一致。
//   - FmChipType 定数は FmEngineApi.h の値を維持しつつ、Nuked が対応する
//     チップを追加で定義する (追加分は 0x10 以降)。
//   - Wasapi_* API はそのまま提供 (Windows 専用。非 Windows では no-op stub)。
//   - 呼び出し側は本ヘッダ 1 枚だけを include すれば使える。
//
// ビルド定義:
//   NUKEDENGINE_EXPORTS  → dllexport  (DLL 本体ビルド時)
//   NUKEDENGINE_STATIC   → 属性なし   (静的リンク時)
//   それ以外             → dllimport  (利用側ビルド時)

#ifndef NUKEDENGINE_API_H
#define NUKEDENGINE_API_H

#include <stdint.h>

// =========================================================
// エクスポート属性マクロ  (FmEngineApi.h の FMENGINE_API と同形)
// =========================================================
#if defined(_WIN32) || defined(_WIN64)
#  if defined(NUKEDENGINE_STATIC)
#    define FMENGINE_API
#  elif defined(NUKEDENGINE_EXPORTS)
#    define FMENGINE_API __declspec(dllexport)
#  else
#    define FMENGINE_API __declspec(dllimport)
#  endif
#  define FMENGINE_CALL __cdecl
#else
#  define FMENGINE_API  __attribute__((visibility("default")))
#  define FMENGINE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// =========================================================
// 不透明ハンドル型  (FmEngineApi.h と同一)
// =========================================================
typedef struct FmEngineOpaque*  FmEngineHandle;
typedef struct WasapiOpaque*    WasapiHandle;

// =========================================================
// エラーコード  (FmEngineApi.h と完全一致)
// =========================================================
typedef enum FmResult {
    FM_OK               =  0,
    FM_ERR_INVALID_ARG  = -1,
    FM_ERR_COM          = -2,
    FM_ERR_AUDIO        = -3,
    FM_ERR_EXCEPTION    = -4,
} FmResult;

// =========================================================
// チップ種別
//
// 0〜3 は FmEngineApi.h の FM_CHIP_* と値を完全一致させる。
// 4 以降は NukedEngine 拡張 (Nuked コアが対応する追加チップ)。
// =========================================================
typedef enum FmChipType {
    // ---- YMEngine / FmEngineApi.h 互換値 (変更不可) ----
    FM_CHIP_OPL2  = 0,   // YM3812  (Nuked-OPL2-Lite)
    FM_CHIP_OPL3  = 1,   // YMF262  (Nuked-OPL3)
    FM_CHIP_OPN2  = 2,   // YM2612  (Nuked-OPN2, YM2612 mode)
    FM_CHIP_OPM   = 3,   // YM2151  (Nuked-OPM)

    // ---- NukedEngine 拡張 ----
    FM_CHIP_OPN2_YM3438 = 4,   // YM3438  (Nuked-OPN2, YM3438 mode)
    FM_CHIP_OPP         = 5,   // YM2164  (Nuked-OPM, OPP flag)
    FM_CHIP_OPLL        = 6,   // YM2413  (Nuked-OPLL)
    FM_CHIP_VRC7        = 7,   // DS1001  (Nuked-OPLL)
    FM_CHIP_OPLL_YM2413B= 8,   // YM2413B (Nuked-OPLL)
    FM_CHIP_OPLL_YMF281 = 9,   // YMF281  (Nuked-OPLL)
    FM_CHIP_OPLL_YMF281B= 10,  // YMF281B (Nuked-OPLL)
    FM_CHIP_OPLL_YM2420 = 11,  // YM2420  (Nuked-OPLL)
    FM_CHIP_OPLL_YM2423 = 12,  // YM2423  (Nuked-OPLL)
    FM_CHIP_PSG         = 13,  // YM7101  (Nuked-PSG / Sega Mega Drive)
} FmChipType;

// =========================================================
// FmEngine API  (FmEngineApi.h と完全一致)
// =========================================================

// エンジン生成。sample_rate: 出力サンプルレート (例: 44100, 48000)
FMENGINE_API FmEngineHandle FMENGINE_CALL
FmEngine_Create(uint32_t sample_rate);

// エンジン破棄
FMENGINE_API void FMENGINE_CALL
FmEngine_Destroy(FmEngineHandle engine);

// チップ追加。clock=0 で各チップの標準クロックを自動選択。
// 成功時: *out_id に chip_id を書き込み FM_OK を返す。
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddChip(FmEngineHandle engine, FmChipType type, uint32_t clock,
                 uint32_t* out_id);

// チップ名を取得 (静的文字列、解放不要)
FMENGINE_API const char* FMENGINE_CALL
FmEngine_GetChipName(FmEngineHandle engine, uint32_t chip_id);

// チップのネイティブサンプルレートを取得
FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetNativeRate(FmEngineHandle engine, uint32_t chip_id);

// エンジンのターゲットサンプルレートを取得
FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetSampleRate(FmEngineHandle engine);

// レジスタ書き込み (任意スレッドから安全)
//   port: OPL3/OPN2 ではバンク選択 (0 or 1)。その他は 0 固定。
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Write(FmEngineHandle engine, uint32_t chip_id,
               uint8_t reg, uint8_t value, uint32_t port);

// ゲイン設定 (線形スケール。1.0 = 0 dB)
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetGain(FmEngineHandle engine, uint32_t chip_id,
                 float gain_l, float gain_r);

// ゲイン取得
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_GetGain(FmEngineHandle engine, uint32_t chip_id,
                 float* out_gain_l, float* out_gain_r);

// サンプル生成 (オーディオスレッドから呼ぶ)
// out_l, out_r: float[samples] の呼び出し側バッファ (上書き・全チップ合算)
FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Generate(FmEngineHandle engine,
                  float* out_l, float* out_r, uint32_t samples);

// =========================================================
// WasapiOutput API  (FmEngineApi.h と完全一致)
// =========================================================

// WASAPI 出力を作成して FmEngine と紐付ける
// exclusive: 0=Shared mode, 1=Exclusive mode
FMENGINE_API WasapiHandle FMENGINE_CALL
Wasapi_Create(FmEngineHandle engine, int exclusive);

// 破棄 (Stop も内部で呼ばれる)
FMENGINE_API void FMENGINE_CALL
Wasapi_Destroy(WasapiHandle wasapi);

// 再生開始
FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Start(WasapiHandle wasapi);

// 再生停止
FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Stop(WasapiHandle wasapi);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NUKEDENGINE_API_H
