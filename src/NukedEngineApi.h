#pragma once
// NukedEngineApi.h
// YMEngine (madscient/YMEngine) の FmEngineApi.h と互換の C ファサード API。
// バックエンドを ymfm から Nuked シリーズエミュレーターに差し替えた実装。
//
// 互換性の方針:
//   - 関数シグネチャ・エラーコード・ハンドル型・エクスポートシンボルは
//     FmEngineApi.h と完全一致。
//   - FmChipType / FmChipTypeExt / FmMemoryType は FmEngineApi.h と値を完全一致させる。
//   - FmChipTypeExt のうち Nuked でサポートしない種別 (SSG/DCSG/SCC/SAA) は
//     FmEngine_AddExtChip に渡すと FM_ERR_INVALID_ARG を返す。
//   - Nuked 固有チップ (OPN2 YM3438 mode, OPP, OPLL 亜種, PSG) は
//     FmChipTypeNuked として 0x200 以降に定義し、FmEngine_AddNukedChip() で追加する。
//   - MSVC C2143/C2059 回避のため typedef enum / struct は extern "C" の外で定義。
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

// =========================================================
//  エラーコード (FmEngineApi.h と完全一致)
//  ※ MSVC C2143 回避のため extern "C" の外で定義
// =========================================================
typedef enum FmResult {
    FM_OK              =  0,
    FM_ERR_INVALID_ARG = -1,
    FM_ERR_COM         = -2,
    FM_ERR_AUDIO       = -3,
    FM_ERR_EXCEPTION   = -4,
} FmResult;

// =========================================================
//  チップ種別 (FmEngineApi.h と完全一致)
//  FmChip.h の ChipType enum と順序・値が一致している。
//  ※ MSVC C2143 回避のため extern "C" の外で定義
// =========================================================
typedef enum FmChipType {
    FM_CHIP_Y8950  =  0,
    FM_CHIP_OPL    =  1,
    FM_CHIP_OPL2   =  2,
    FM_CHIP_OPL3   =  3,
    FM_CHIP_OPL4   =  4,
    FM_CHIP_OPN    =  5,
    FM_CHIP_OPNA   =  6,
    FM_CHIP_OPNB   =  7,
    FM_CHIP_OPNBB  =  8,
    FM_CHIP_OPN2   =  9,
    FM_CHIP_OPM    = 10,
    FM_CHIP_OPLL   = 11,
    FM_CHIP_OPLLP  = 12,
    FM_CHIP_OPLLX  = 13,
    FM_CHIP_OPZ    = 14,
    FM_CHIP_VRC7   = 15,
} FmChipType;

// =========================================================
//  外部ライブラリチップ種別 (FmEngineApi.h と完全一致)
//  NukedEngine では FmEngine_AddExtChip() に渡すと FM_ERR_INVALID_ARG を返す。
//  ※ MSVC C2143 回避のため extern "C" の外で定義
// =========================================================
typedef enum FmChipTypeExt {
    FM_CHIP_EXT_SSG  = 100,
    FM_CHIP_EXT_DCSG = 101,
    FM_CHIP_EXT_SCC  = 102,
    FM_CHIP_EXT_SAA  = 103,
} FmChipTypeExt;

// =========================================================
//  外部メモリアクセス種別 (FmEngineApi.h と完全一致)
//  ymfm::access_class と値を一致させる。
//  ※ MSVC C2143 回避のため extern "C" の外で定義
// =========================================================
typedef enum FmMemoryType {
    FM_MEM_IO      = 0,
    FM_MEM_ADPCM_A = 1,
    FM_MEM_ADPCM_B = 2,
    FM_MEM_PCM     = 3,
} FmMemoryType;

// =========================================================
//  Nuked 固有チップ種別 (NukedEngine 拡張)
//  FmEngine_AddNukedChip() で使用する。0x200 以降に配置し
//  FmChipType / FmChipTypeExt との衝突を回避する。
//  ※ MSVC C2143 回避のため extern "C" の外で定義
// =========================================================
typedef enum FmChipTypeNuked {
    FM_NUKED_OPN2C    = 0x200,  // YM3438     (Nuked-OPN2, YM3438 mode)
    FM_NUKED_OPP      = 0x201,  // YM2164     (Nuked-OPM, OPP flag)
    FM_NUKED_OPLLP_B  = 0x202,  // YMF281B    (Nuked-OPLL) ※FM_CHIP_OPLLP(YMF281) の B バリアント
    FM_NUKED_OPLL2    = 0x203,  // YM2420     (Nuked-OPLL) ※FmChipType に存在しない亜種
    FM_NUKED_OPLL_B   = 0x204,  // YM2413B    (Nuked-OPLL) ※opll_type_ym2413b は ym2413 と内部処理が異なる
    // 以下は FmChipType / FmChipTypeExt と機能重複のため廃止:
    //   FM_CHIP_OPLL  (=11) → YM2413  (opll_type_ym2413)
    //   FM_CHIP_OPLLP (=12) → YMF281  (opll_type_ymf281)
    //   FM_CHIP_OPLLX (=13) → YM2423  (opll_type_ym2423)
    //   FM_CHIP_EXT_DCSG(=101) → SN76489/PSG
} FmChipTypeNuked;

// =========================================================
//  不透明ハンドル前方宣言 (FmEngineApi.h と同一)
//  ※ extern "C" の外に置く
// =========================================================
struct FmEngineOpaque;

// =========================================================
//  ハンドル typedef と関数宣言のみ extern "C" に入れる
// =========================================================
#ifdef __cplusplus
extern "C" {
#endif

typedef struct FmEngineOpaque* FmEngineHandle;

// ----- FmEngine API (FmEngineApi.h と完全一致) -----

FMENGINE_API FmEngineHandle FMENGINE_CALL FmEngine_Create(uint32_t sample_rate);
FMENGINE_API void           FMENGINE_CALL FmEngine_Destroy(FmEngineHandle engine);
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_AddChip(
    FmEngineHandle engine, FmChipType type, uint32_t clock, uint32_t* out_id);

// 外部ライブラリチップ追加
// FM_CHIP_EXT_DCSG のみサポート (Nuked-PSG / YM7101 をバックエンドとして使用)
// FM_CHIP_EXT_SSG / FM_CHIP_EXT_SCC / FM_CHIP_EXT_SAA は FM_ERR_INVALID_ARG を返す
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_AddExtChip(
    FmEngineHandle engine, FmChipTypeExt type, uint32_t clock, uint32_t* out_id);

FMENGINE_API const char*    FMENGINE_CALL FmEngine_GetChipName(
    FmEngineHandle engine, uint32_t chip_id);
FMENGINE_API uint32_t       FMENGINE_CALL FmEngine_GetNativeRate(
    FmEngineHandle engine, uint32_t chip_id);
FMENGINE_API uint32_t       FMENGINE_CALL FmEngine_GetSampleRate(
    FmEngineHandle engine);
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_Write(
    FmEngineHandle engine, uint32_t chip_id, uint8_t reg, uint8_t value, uint32_t port);
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id, float gain_l, float gain_r);
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id, float* out_gain_l, float* out_gain_r);

// 外部メモリ設定 (NukedEngine では未サポート → FM_ERR_INVALID_ARG)
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, const uint8_t* data, uint32_t size);
FMENGINE_API uint32_t       FMENGINE_CALL FmEngine_GetMemorySize(
    FmEngineHandle engine, uint32_t chip_id, FmMemoryType mem_type);

// 波形を生成して out_l / out_r に書き込む (float32, [-1.0, 1.0])
// スレッドセーフ: 任意スレッドから FmEngine_Write と並行して呼び出し可能。
// アプリケーションのオーディオコールバックからこの関数を呼び出すこと。
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_Generate(
    FmEngineHandle engine, float* out_l, float* out_r, uint32_t samples);

// ----- NukedEngine 拡張 API -----

// Nuked 固有チップを追加する (YM3438 mode, OPP, OPLL 亜種, PSG)
// type: FmChipTypeNuked のいずれか
// clock: 0 で標準クロック自動選択
// 成功時: *out_id に chip_id を書き込み FM_OK を返す
FMENGINE_API FmResult       FMENGINE_CALL FmEngine_AddNukedChip(
    FmEngineHandle engine, FmChipTypeNuked type, uint32_t clock, uint32_t* out_id);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NUKEDENGINE_API_H
