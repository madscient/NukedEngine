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

## sample_app による JSON 駆動テスト

`example/sample_app.cpp` は、JSON ファイルでチップ初期化・発音シーケンスを記述し、  
WASAPI でリアルタイム再生しながら検証できるテストツールです。

```bash
sample_app.exe [オプション] [file1.json] [file2.json] ...
```

| オプション | 内容 |
|---|---|
| (引数なし) | `patches/all.json` を使用 |
| `file.json ...` | 指定した JSON ファイルを順に処理 (複数指定可) |
| `-r <rate>` | サンプルレートを指定 (省略時 48000) |
| `-d <name>` | デバイス名を部分一致で指定 (省略時デフォルトデバイス) |

### JSON フォーマット

```json
{
  "sample_rate": 48000,
  "global": { "note_ms": 800, "rest_ms": 200 },
  "chips": {
    "OPL2": { "gain": 1.0, "init": [...], "channels": [...] },
    "OPNA": { "gain_l": 0.8, "gain_r": 1.0, "init": [...], "channels": [...] },
    "OPM":  { "$ref": "opm.json" }
  }
}
```

| キー | 内容 |
|---|---|
| `gain` | L/R 共通ゲイン (省略時 1.0) |
| `gain_l` / `gain_r` | 左右個別ゲイン。指定時は `gain` より優先される |
| `$ref` | 他の JSON ファイル内の同名チップ定義を参照する (下記参照) |

### `$ref` によるチップ定義の外部参照

`{"$ref": "other.json"}` と書くと、参照先ファイルの `chips.<同名チップ>` の定義を  
そのまま読み込んで使用します。参照先パスは参照元ファイルからの相対パスです。

```json
// all.json
{
  "chips": {
    "OPM":  { "$ref": "opm.json" },
    "OPLL": { "$ref": "opll.json" }
  }
}
```

これにより、各チップ専用の JSON ファイル (`opm.json` / `opll.json` など) を単体テストにも  
`all.json` からの一括テストにも使い回せます。`$ref` は入れ子 (参照先がさらに `$ref` を持つ) にも  
対応していますが、循環参照は検出して警告を出しスキップします (最大ネスト深度 8)。

### 注意

`FmEngine_AddChip` / `FmEngine_AddExtChip` / `FmEngine_AddNukedChip` は  
`Wasapi_Start` より前に全て完了させてください (WASAPI スレッドとの競合防止)。  
`sample_app.cpp` は全 JSON ファイルのチップ追加を済ませてから WASAPI を開始する構成になっています。

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
| sample_app の `gain_l`/`gain_r`・`$ref` | ✅ | ✅ (本バージョンで追加) |

## OPM / OPLL のサンプリング方式について

Nuked-OPM と Nuked-OPLL は、ymfm のような「1サンプル単位の数学的モデル」ではなく、  
実チップのクロック単位の内部パイプラインをそのままシミュレートする cycle-accurate 実装です。  
そのため、ymfm と同一のレジスタ値を書き込んでも、素朴に 1 クロックずつ進めて出力を取り出すだけでは  
**実際の音程の整数倍（2倍 / 4倍）の周波数になる**という、両コア共通の問題があります。  
NukedEngine では以下の方式でこれを補正し、ymfm (YMEngine) と同じ音程・出力レートを再現しています。

### OPM (YM2151)

- OPM の DAC はシリアル出力で、L チャンネルは **sh2 信号の立ち下がり**（32 クロック間隔）ごとに  
  1 サンプル更新される。
- 1 サンプルあたり 64 クロックだけ進めて出力を読むと、その間に sh2 の立ち下がりが 2 回発生し、  
  2 回目の値は 1 回目に対して半周期分進んだ値になる。これをそのままサンプル列として使うと、  
  実効的な音程が **1 オクターブ高く** なる。
- 対策として、sh2 の立ち下がりを **2 回検出するたびに 1 サンプル**を生成する（`opm_prev_sh2` で  
  立ち下がり状態をサンプル境界をまたいで保持）。結果として `nativeRate = clock / 128` となる。
- レジスタ書き込み (`write_direct_full`) は OPM の全 32 スロットを 1 周させるため 64 クロック×2 フェーズ  
  (アドレス・データ) 必要で、消費したクロック分はリサンプラー手前で無音 (`skip_samples`) として  
  扱うことでノイズの発生を防いでいる。

### OPLL (YM2413)

- Nuked-OPLL の内部位相生成器は 19bit 位相カウンタで駆動されており、ymfm 等が前提とする  
  「フルレート (`clock/72`) を基準にした fnum/block 計算式」に対して **4 倍速**で位相が進む。  
  そのため素朴に 72 クロック (=1 ymfm サンプル相当) ごとにキャリア出力を読むと、音程が 4 倍 (2 オクターブ)  
  高くなる。
- 対策として、1 サンプルあたり実際には **18 クロックだけ**チップを進める (`nativeRate` の分母としては  
  従来通り `clock/72` を使うが、実クロック消費は 1/4 にする)。これにより ymfm と同じ音程が得られる。
- OPLL は 18 クロックの 1 サイクル内で、チャンネルごとに異なるタイミングでキャリア出力が  
  `buffer[0]` (output_m) に現れる。実測で特定したマッピングは以下の通り。

  | チャンネル | CH1 | CH2 | CH3 | CH4 | CH5 | CH6 | CH7 | CH8 | CH9 |
  |---|---|---|---|---|---|---|---|---|---|
  | cycles | 8 | 9 | 10 | 14 | 15 | 16 | 2 | 3 | 4 |

  全 9 チャンネルを正しくミックスするには、この 9 つの cycles で得られる値をすべて合算する必要がある。
- リズムモード (reg `0x0E` bit5) 使用時、BD/SD/TOM/HH/Cymbal の出力はメロディチャンネルとは別に  
  `buffer[1]` (output_r) に現れる。リズムモード OFF 時の `output_r` は符号のみのダミー値で実質無音のため、  
  常時加算してもメロディ専用利用時の音質には影響しない。
- YM2413 は 9bit DAC (出力振幅 ±256 程度) のため、16bit フルスケールに正規化するには  
  128 倍 (`32768/256`) のゲインが必要。最大 9 チャンネル分を合算するため、チャンネル数で除算して  
  クリッピングを防いでいる。
- レジスタ書き込み (`write_direct_full`) は 72 クロック×2 フェーズ消費する。gen_native 側の実消費が  
  18 クロック/サンプルであるため、`144 / 18 = 8` サンプル分を `skip_samples` として無音化している。

> これらの実装はいずれも madscient と Claude による実機波形比較・FFT 解析を通じて  
> 経験的に導出したものであり、Nuked-OPM/Nuked-OPLL のソースコードの挙動を忠実に再現する一方で、  
> パッチデータ (`example/patches/*.json`) は YMEngine (ymfm) 用のものをそのまま流用できる。
