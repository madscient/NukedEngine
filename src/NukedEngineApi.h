#pragma once
// NukedEngineApi.h
// FmEngineApi の C インターフェースを Nuked シリーズエミュレーターで実装したエンジンの公開ヘッダ。
//
// 宣言は FmEngineApi.h と同じ (正本は https://github.com/madscient/FMEngineTest の
// include/FmEngineApi.h、仕様は同じリポジトリの docs/FmEngineApi.md)。違いは次の 2 点。
//   - エクスポート属性を下のビルド定義で切り替える
//   - 外部メモリの関数 (FmEngine_GetMemoryCount / FmEngine_GetMemoryName /
//     FmEngine_SetMemory / FmEngine_SetMemoryEx) を持たない。NukedEngine のチップは
//     どれも外部メモリを持たない
//
// チップと部位はキーワード文字列で指定する ("OPM"、"MELODY" 等)。
// 一覧は FmEngine_Inquiry / FmEngine_GetSupportedChip と、FmEngine_GetPartCount /
// FmEngine_GetPartName で取得できる。
//
// ビルド定義:
//   NUKEDENGINE_EXPORTS → dllexport (DLL 本体ビルド時)
//   NUKEDENGINE_STATIC  → 属性なし (静的リンク時)
//   それ以外            → dllimport (利用側ビルド時)

#ifndef NUKEDENGINE_API_H
#define NUKEDENGINE_API_H

#include <stdint.h>

// =========================================================
//  エクスポート属性マクロ (FmEngineApi.h の FMENGINE_API/FMENGINE_CALL と同形)
// =========================================================
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(NUKEDENGINE_STATIC)
#    define FMENGINE_API
#  elif defined(NUKEDENGINE_EXPORTS)
#    define FMENGINE_API __declspec(dllexport)
#  else
#    define FMENGINE_API __declspec(dllimport)
#  endif
#  define FMENGINE_CALL __cdecl
#else
#  if defined(NUKEDENGINE_EXPORTS) && defined(__GNUC__)
#    define FMENGINE_API __attribute__((visibility("default")))
#  else
#    define FMENGINE_API
#  endif
#  define FMENGINE_CALL
#endif

// =========================================================
//  戻り値コード (FmEngineApi.h と完全一致)
// =========================================================
typedef enum FmResult {
    FM_OK                =  0,
    FM_ERR_INVALID_ARG   = -1,
    FM_ERR_UNKNOWN_CHIP  = -2,  // FmEngine_AddChip で未知のチップ名
    FM_ERR_ALLOC         = -3,
    FM_ERR_UNAVAILABLE   = -4,
} FmResult;

// =========================================================
//  不透明ハンドル
// =========================================================
struct FmEngineOpaque;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FmEngineOpaque* FmEngineHandle;

// =========================================================
//  エンジン生成・破棄
// =========================================================
FMENGINE_API FmEngineHandle FMENGINE_CALL FmEngine_Create(uint32_t sample_rate);
FMENGINE_API void           FMENGINE_CALL FmEngine_Destroy(FmEngineHandle engine);

// =========================================================
//  対応チップ問い合わせ
//  FmEngine_Inquiry        : 対応チップの総数を返す。
//  FmEngine_GetSupportedChip: index 番目のチップ名を返す (範囲外は NULL)。
// =========================================================
FMENGINE_API uint32_t    FMENGINE_CALL FmEngine_Inquiry(FmEngineHandle engine);
FMENGINE_API const char* FMENGINE_CALL FmEngine_GetSupportedChip(
    FmEngineHandle engine, uint32_t index);

// =========================================================
//  チップ追加
//  name  : チップ名文字列 ("OPM", "OPLL", "OPL3" 等、大文字小文字を区別する)
//  clock : マスタークロック Hz。既定値は無く、0 なら FM_ERR_INVALID_ARG を返す。
//  未知の名前なら FM_ERR_UNKNOWN_CHIP を返す。
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL FmEngine_AddChip(
    FmEngineHandle engine, const char* name, uint32_t clock, uint32_t* out_id);

// =========================================================
//  チップ情報取得
// =========================================================
FMENGINE_API const char* FMENGINE_CALL FmEngine_GetChipName(
    FmEngineHandle engine, uint32_t chip_id);
// ネイティブサンプルレート (Hz、端数切り捨て)。
FMENGINE_API uint32_t    FMENGINE_CALL FmEngine_GetNativeRate(
    FmEngineHandle engine, uint32_t chip_id);
FMENGINE_API uint32_t    FMENGINE_CALL FmEngine_GetSampleRate(
    FmEngineHandle engine);

// =========================================================
//  レジスタ書き込み
//  スレッドセーフ: オーディオコールバックスレッドと並行して呼び出し可能。
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL FmEngine_Write(
    FmEngineHandle engine, uint32_t chip_id,
    uint8_t reg, uint8_t value, uint32_t port);

// =========================================================
//  ゲイン設定 (L/R 独立)
//  1.0 = 0 dB。オーディオコールバックスレッドと並行して呼び出し可能。
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id, float gain_l, float gain_r);
FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float* out_gain_l, float* out_gain_r);

// =========================================================
//  部位ごとのゲイン (L/R 独立)
//  部位は、チップが別々の端子から出す出力。名前の文字列で指定する
//  (大文字小文字を区別する)。
//  NukedEngine で部位を持つのは OPLL 系 ("MELODY" / "RHYTHM") と OPL3 ("AB" / "CD") だけ。
//  既定値は 1.0 ("CD" は 0)。
// =========================================================
// チップが持つ部位の数。部位を持たないチップと未知の chip_id は 0。
FMENGINE_API uint32_t    FMENGINE_CALL FmEngine_GetPartCount(
    FmEngineHandle engine, uint32_t chip_id);
// index 番目の部位の名前 (範囲外と未知の chip_id は NULL)。
// 文字列は FmEngine_Destroy が戻るまで有効。
FMENGINE_API const char* FMENGINE_CALL FmEngine_GetPartName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index);
// 実際に掛かるゲインは FmEngine_SetGain のゲイン × 部位のゲイン。
// 未知の chip_id、チップが持たない部位の名前、NULL は FM_ERR_INVALID_ARG。
// オーディオコールバックスレッドと並行して呼び出し可能。
FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float gain_l, float gain_r);
FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float* out_gain_l, float* out_gain_r);

// =========================================================
//  波形生成
//  out_l / out_r : float32 非インターリーブ、範囲 [-1.0, 1.0]
//  アプリケーションのオーディオコールバックから呼び出すこと。
// =========================================================
FMENGINE_API FmResult FMENGINE_CALL FmEngine_Generate(
    FmEngineHandle engine, float* out_l, float* out_r, uint32_t samples);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NUKEDENGINE_API_H
