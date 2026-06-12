# NukedEngine

[YMEngine](https://github.com/madscient/YMEngine) (`FmEngineApi.h`) と互換の C ファサード API を持つ  
FM サウンドチップエミュレーションエンジンです。  
バックエンドを ymfm から **Nuked シリーズエミュレーター**に差し替えています。

## YMEngine との互換性

`FmEngineApi.h` の include を `NukedEngineApi.h` に置き換えるだけで、  
呼び出し側コードを変更せずに Nuked コアに切り替えられます。

| 項目 | YMEngine | NukedEngine |
|---|---|---|
| ヘッダー | `FmEngineApi.h` | `NukedEngineApi.h` |
| 関数名・シグネチャ | `FmEngine_*` / `Wasapi_*` | **完全一致** |
| ハンドル型 | `FmEngineHandle` / `WasapiHandle` | **完全一致** |
| エラーコード | `FmResult` / `FM_OK` / `FM_ERR_*` | **完全一致** |
| `FmChipType` 値 | `FM_CHIP_Y8950=0` 〜 `FM_CHIP_VRC7=15` | **完全一致** |
| `FmChipTypeExt` 値 | `FM_CHIP_EXT_SSG=100` 〜 `FM_CHIP_EXT_SAA=103` | **完全一致** (`DCSG` のみサポート) |
| `FmMemoryType` 値 | `FM_MEM_IO=0` 〜 `FM_MEM_PCM=3` | **完全一致** (未サポート) |
| MSVC `extern "C"` 外 enum | ✅ | **完全一致** |
| エクスポートシンボル | 23 関数 | **完全一致** + 拡張 1 関数 |

### 未サポート機能

以下は YMEngine にあり NukedEngine では対応していません。呼び出すと `FM_ERR_INVALID_ARG` を返します。

- `FmEngine_AddExtChip` の `FM_CHIP_EXT_SSG` / `FM_CHIP_EXT_SCC` / `FM_CHIP_EXT_SAA`
- `FmEngine_SetMemory` / `FmEngine_GetMemorySize` (ADPCM/PCM ROM/RAM)

## 対応チップ

### FmEngine_AddChip() — FmChipType

| 定数 | 値 | チップ | バックエンド | ライセンス |
|---|---|---|---|---|
| `FM_CHIP_OPL2` | 2 | YM3812 | Nuked-OPL2-Lite | LGPL-2.1 |
| `FM_CHIP_OPL3` | 3 | YMF262 | Nuked-OPL3 | LGPL-2.1 |
| `FM_CHIP_OPN2` | 9 | YM2612 | Nuked-OPN2 | LGPL-2.1 |
| `FM_CHIP_OPM` | 10 | YM2151 | Nuked-OPM | LGPL-2.1 |
| `FM_CHIP_OPLL` | 11 | YM2413 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLLP` | 12 | YMF281 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLLX` | 13 | YM2423 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_VRC7` | 15 | DS1001 | Nuked-OPLL | GPL-2.0 |

その他の値 (`Y8950` / `OPL` / `OPL4` / `OPN` / `OPNA` / `OPNB` / `OPNBB` / `OPZ`) は  
Nuked コアが対応していないため `FM_ERR_INVALID_ARG` を返します。

### FmEngine_AddExtChip() — FmChipTypeExt

| 定数 | 値 | チップ | バックエンド | ライセンス |
|---|---|---|---|---|
| `FM_CHIP_EXT_DCSG` | 101 | YM7101 (PSG) | Nuked-PSG | GPL-2.0 |

### FmEngine_AddNukedChip() — FmChipTypeNuked (NukedEngine 拡張)

YMEngine の `FmChipType` / `FmChipTypeExt` に存在しない Nuked 固有チップを追加するための API です。

| 定数 | 値 | チップ | バックエンド | ライセンス |
|---|---|---|---|---|
| `FM_NUKED_OPN2C` | 0x200 | YM3438 | Nuked-OPN2 (YM3438 mode) | LGPL-2.1 |
| `FM_NUKED_OPP` | 0x201 | YM2164 | Nuked-OPM (OPP flag) | LGPL-2.1 |
| `FM_NUKED_OPLLP_B` | 0x202 | YMF281B | Nuked-OPLL | GPL-2.0 |
| `FM_NUKED_OPLL2` | 0x203 | YM2420 | Nuked-OPLL | GPL-2.0 |
| `FM_NUKED_OPLL_B` | 0x204 | YM2413B | Nuked-OPLL (`opll_type_ym2413b`) | GPL-2.0 |

> **ライセンス**: OPLL・PSG コアは GPL-2.0 のため、組み込む場合は結合成果物全体が GPL-2.0 になります。

## ディレクトリ構成

```
NukedEngine/
├── include/
│   └── NukedEngineApi.h       ← 公開ヘッダー (YMEngine FmEngineApi.h 互換)
├── src/
│   ├── NukedEngineApi.cpp     ← 実装
│   └── NukedEngineApi.def     ← MSVC エクスポート定義
├── example/
│   └── sample_app.cpp         ← 使用例 (WASAPI リアルタイム再生)
├── CMakeLists.txt
└── cores/                     ← Nuked コア (git submodule)
    ├── opl3/  (Nuked-OPL3)
    ├── opl2/  (Nuked-OPL2-Lite)
    ├── opn2/  (Nuked-OPN2)
    ├── opm/   (Nuked-OPM)
    ├── opll/  (Nuked-OPLL)
    └── psg/   (Nuked-PSG)
```

## セットアップ

Nuked コアは Git サブモジュールとして管理されています。

```bash
# 新規クローン時 (サブモジュールを一括取得)
git clone --recurse-submodules https://github.com/madscient/NukedEngine.git

# 既存クローンにサブモジュールを後から追加する場合
git submodule update --init --recursive
```

CMake 設定時にサブモジュールが未取得であれば自動的に初期化します。

```bash
# ビルド (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 成果物
#   build/bin/NukedEngineApi.dll
#   build/lib/NukedEngineApi.lib
#   build/bin/sample_app.exe

# ビルド (Linux / macOS)  ※ WASAPI は Windows 専用
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 使い方

### 基本パターン (YMEngine と完全同形)

```c
#include "NukedEngineApi.h"

// エンジン作成
FmEngineHandle eng = FmEngine_Create(48000);

// ymfm 互換チップを追加
uint32_t opl3_id;
FmEngine_AddChip(eng, FM_CHIP_OPL3, 0, &opl3_id);
FmEngine_SetGain(eng, opl3_id, 1.0f, 1.0f);

// レジスタ書き込み (任意スレッドから安全)
FmEngine_Write(eng, opl3_id, 0x05, 0x01, 1); // OPL3 enable
FmEngine_Write(eng, opl3_id, 0xB0, 0x32, 0); // Key-On

// WASAPI リアルタイム再生 (Windows)
WasapiHandle wasapi = Wasapi_Create(eng, 0 /*Shared mode*/);
Wasapi_Start(wasapi);
// ... 再生中も FmEngine_Write で任意タイミングにレジスタ更新可能
Wasapi_Stop(wasapi);
Wasapi_Destroy(wasapi);

FmEngine_Destroy(eng);
```

### PSG (DCSG) の追加

```c
// FM_CHIP_EXT_DCSG → Nuked-PSG (YM7101) がバックエンド
uint32_t psg_id;
FmEngine_AddExtChip(eng, FM_CHIP_EXT_DCSG, 0, &psg_id);
FmEngine_SetGain(eng, psg_id, 0.5f, 0.5f);
FmEngine_Write(eng, psg_id, 0x00, 0x8F, 0); // CH0 tone latch
FmEngine_Write(eng, psg_id, 0x00, 0x07, 0); // CH0 tone high
FmEngine_Write(eng, psg_id, 0x00, 0x90, 0); // CH0 volume max
```

### NukedEngine 固有チップの追加

```c
// YM3438 モード (OPN2C)
uint32_t ym3438_id;
FmEngine_AddNukedChip(eng, FM_NUKED_OPN2C, 0, &ym3438_id);

// YM2413B (OPLL-B、opll_type_ym2413b で初期化)
uint32_t opll_b_id;
FmEngine_AddNukedChip(eng, FM_NUKED_OPLL_B, 0, &opll_b_id);
```

### WASAPI デバイス選択

```c
// 利用可能なデバイスを列挙
uint32_t n = Wasapi_GetDeviceCount();
for (uint32_t i = 0; i < n; i++) {
    wchar_t id[128], name[128];
    Wasapi_GetDeviceId(i, id, 128);
    Wasapi_GetDeviceName(i, name, 128);
    wprintf(L"[%u]%s %ls\n", i, Wasapi_IsDefaultDevice(i) ? "*" : " ", name);
}

// デバイスを指定して作成
wchar_t device_id[128];
Wasapi_GetDeviceId(0, device_id, 128);
WasapiHandle wasapi = Wasapi_CreateWithDevice(eng, 0, device_id);
```

## YMEngine との実装上の差異

| 機能 | YMEngine | NukedEngine |
|---|---|---|
| FM コア | ymfm (cycle-approximate) | Nuked (cycle-accurate) |
| リサンプリング | LinearResampler (線形補間) | 同等の LinearResampler を内蔵 |
| マルチチップ | 複数 AddChip → Generate で合算 | **同一** |
| スレッド安全性 | SPSC ライトキュー | **同一** |
| WASAPI | Shared/Exclusive・デバイス選択対応 | **同一** |
| ソフトクリップ | `FmEngine::generate()` 内で適用 | `FmEngine_Generate()` では**非適用**、WASAPI レンダリングループで `clamp1()` を適用 |
| 外部メモリ (ADPCM/PCM) | ✅ | ❌ 未サポート |
| 外部チップ (SSG/SCC/SAA) | ✅ (emu2149/emu2212/SAASound) | ❌ 未サポート |
| DCSG (PSG) | ✅ (emu76489) | ✅ (Nuked-PSG) |
