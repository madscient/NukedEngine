# NukedEngine

[YMEngine](https://github.com/madscient/YMEngine) (`FmEngineApi.h`) と完全互換の C ファサード API を持つ  
FM サウンドチップエミュレーションエンジンです。  
バックエンドを ymfm から **Nuked シリーズエミュレーター**に差し替えています。

## YMEngine との互換性

| 項目 | YMEngine | NukedEngine |
|---|---|---|
| ヘッダー | `FmEngineApi.h` | `NukedEngineApi.h` |
| 関数名 | `FmEngine_*` / `Wasapi_*` | **完全一致** |
| ハンドル型 | `FmEngineHandle` / `WasapiHandle` | **完全一致** |
| エラーコード | `FmResult` / `FM_OK` / `FM_ERR_*` | **完全一致** |
| 既存チップ定数 | `FM_CHIP_OPL2=0` … `FM_CHIP_OPM=3` | **完全一致** |
| 追加チップ定数 | (なし) | `FM_CHIP_OPN2_YM3438=4` … `FM_CHIP_PSG=13` |

既存の `FmEngineApi.h` を `NukedEngineApi.h` に置き換えるだけで、  
呼び出し側コードを変更せずに Nuked コアに切り替えられます。

## 対応チップ

| 定数 | チップ | バックエンド | ライセンス |
|---|---|---|---|
| `FM_CHIP_OPL2` (=0) | YM3812 | Nuked-OPL2-Lite | LGPL-2.1 |
| `FM_CHIP_OPL3` (=1) | YMF262 | Nuked-OPL3 | LGPL-2.1 |
| `FM_CHIP_OPN2` (=2) | YM2612 | Nuked-OPN2 | LGPL-2.1 |
| `FM_CHIP_OPM` (=3) | YM2151 | Nuked-OPM | LGPL-2.1 |
| `FM_CHIP_OPN2_YM3438` (=4) | YM3438 | Nuked-OPN2 | LGPL-2.1 |
| `FM_CHIP_OPP` (=5) | YM2164 | Nuked-OPM | LGPL-2.1 |
| `FM_CHIP_OPLL` (=6) | YM2413 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_VRC7` (=7) | DS1001 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLL_YM2413B` (=8) | YM2413B | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLL_YMF281` (=9) | YMF281 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLL_YMF281B` (=10) | YMF281B | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLL_YM2420` (=11) | YM2420 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_OPLL_YM2423` (=12) | YM2423 | Nuked-OPLL | GPL-2.0 |
| `FM_CHIP_PSG` (=13) | YM7101 | Nuked-PSG | GPL-2.0 |

> **ライセンス**: OPLL・PSG は GPL-2.0 のため、組み込む場合は結合成果物全体が GPL-2.0 になります。

## ディレクトリ構成

```
NukedEngine/
├── include/
│   └── NukedEngineApi.h       ← 公開ヘッダー (YMEngine FmEngineApi.h 互換)
├── src/
│   ├── NukedEngineApi.cpp     ← 実装
│   └── NukedEngineApi.def     ← MSVC エクスポート定義
├── example/
│   └── sample_app.cpp         ← 使用例 (YMEngine main.cpp と同形)
├── CMakeLists.txt
└── cores/                     ← Nuked コアをここに配置
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
git clone --recurse-submodules https://github.com/yourname/NukedEngine.git

# 既存クローンにサブモジュールを後から追加する場合
git submodule update --init --recursive
```

CMake 設定時にサブモジュールが未取得であれば自動的に `git submodule update --init --recursive` を実行します。

```bash
# ビルド (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 成果物
#   build/bin/NukedEngineApi.dll
#   build/lib/NukedEngineApi.lib
#   build/bin/sample_app.exe

# ビルド (Linux / macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 使い方 (YMEngine と完全同形)

```c
#include "NukedEngineApi.h"

// エンジン作成
FmEngineHandle eng = FmEngine_Create(48000);

// チップ追加 (clock=0 で標準クロック自動選択)
uint32_t opl3_id;
FmEngine_AddChip(eng, FM_CHIP_OPL3, 0, &opl3_id);

// ゲイン設定
FmEngine_SetGain(eng, opl3_id, 1.0f, 1.0f);

// レジスタ書き込み (port=1 で OPL3 バンク 1)
FmEngine_Write(eng, opl3_id, 0x05, 0x01, 1); // OPL3 enable
FmEngine_Write(eng, opl3_id, 0xB0, 0x32, 0); // Key-On

// サンプル生成
float l[512], r[512];
FmEngine_Generate(eng, l, r, 512);

// WASAPI 再生 (Windows のみ)
WasapiHandle wasapi = Wasapi_Create(eng, 0);
Wasapi_Start(wasapi);
// ... (任意スレッドから FmEngine_Write 可能)
Wasapi_Stop(wasapi);
Wasapi_Destroy(wasapi);

FmEngine_Destroy(eng);
```

## YMEngine との実装上の差異

| 機能 | YMEngine | NukedEngine |
|---|---|---|
| FMコア | ymfm (cycle-approximate) | Nuked (cycle-accurate) |
| リサンプリング | LinearResampler (線形補間) | 同等の LinearResampler を内蔵 |
| マルチチップ | 複数 AddChip → Generate で合算 | **同一** |
| スレッド安全性 | SPSC ライトキュー | **同一** |
| WASAPI | Shared/Exclusive 対応 | **同一** |
| 対応チップ数 | OPL2/OPL3/OPN2/OPM | 上記 14 種 |
