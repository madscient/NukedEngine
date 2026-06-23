# NukedEngine

[YMEngine](https://github.com/madscient/YMEngine) (`FmEngineApi.h`) および  
[FMEngineTest](https://github.com/madscient/FMEngineTest) と互換の C ファサード API を持つ  
FM サウンドチップエミュレーションエンジン DLL です。  
バックエンドを ymfm から **Nuked シリーズエミュレーター**に差し替えています。  
DLL は波形生成・リサンプリングまでを担い、オーディオ出力はアプリケーション側が担当します。

**License: [GNU General Public License v2.0](LICENSE)**  
Nuked-OPLL および Nuked-PSG が GPL-2.0 であるため、本プロジェクト全体も GPL-2.0 で配布します。

## YMEngine / FMEngineTest との互換性

`NukedEngineApi.dll` は `FmEngineApi.dll` と同一のエクスポートシンボルを持ちます。  
FMEngineTest の `-e` オプションで差し替えるだけで、パッチ JSON を変更せずに  
Nuked コアで再生できます。

```
FMEngineTest.exe -e NukedEngineApi.dll patches/opm.json
```

静的リンク時は `NukedEngineApi.h` の include を `FmEngineApi.h` の代わりに使用してください。

| 項目 | FmEngineApi (YMEngine) | NukedEngineApi |
|---|---|---|
| ヘッダー | `FmEngineApi.h` | `NukedEngineApi.h` |
| 関数名・シグネチャ | `FmEngine_*` 14 関数 | **完全一致** |
| ハンドル型 | `FmEngineHandle` | **完全一致** |
| エラーコード | `FM_OK` / `FM_ERR_*` | **完全一致** |
| チップ指定方法 | 文字列 (`"OPM"`, `"OPLL"` 等) | **完全一致** |
| `FmMemoryType` 値 | `FM_MEM_ADPCM_A/B`, `FM_MEM_PCM` | **完全一致** (未サポート) |
| エクスポートシンボル数 | 14 | **14 (完全一致)** |

### 未サポート機能

以下は YMEngine にあり NukedEngine では対応していません。

| 機能 | 戻り値 |
|---|---|
| `FmEngine_SetMemory` / `FmEngine_GetMemorySize` (ADPCM/PCM ROM/RAM) | `FM_ERR_UNAVAILABLE` / `0` |

FMEngineTest の `patches/opna.json` 等 ADPCM を使うパッチは ADPCM 部分が無音になります。  
それ以外のパッチはそのまま動作します。

## 対応チップ

チップは文字列名で指定します。`FmEngine_Inquiry()` で一覧取得、`FmEngine_AddChip()` で追加します。

| 名前 | チップ | バックエンド | コアライセンス |
|---|---|---|---|
| `"OPL2"` | YM3812 | Nuked-OPL2-Lite | LGPL-2.1 |
| `"OPL3"` | YMF262 | Nuked-OPL3 | LGPL-2.1 |
| `"OPN2"` | YM2612 | Nuked-OPN2 | LGPL-2.1 |
| `"OPN2C"` | YM3438 | Nuked-OPN2 (YM3438 mode) | LGPL-2.1 |
| `"OPM"` | YM2151 | Nuked-OPM | LGPL-2.1 |
| `"OPP"` | YM2164 | Nuked-OPM (OPP flag) | LGPL-2.1 |
| `"OPLL"` | YM2413 | Nuked-OPLL | **GPL-2.0** |
| `"OPLL-B"` | YM2413B | Nuked-OPLL | **GPL-2.0** |
| `"OPLLP"` | YMF281 | Nuked-OPLL | **GPL-2.0** |
| `"OPLLP-B"` | YMF281B | Nuked-OPLL | **GPL-2.0** |
| `"OPLL2"` | YM2420 | Nuked-OPLL | **GPL-2.0** |
| `"OPLLX"` | YM2423 | Nuked-OPLL | **GPL-2.0** |
| `"VRC7"` | DS1001 | Nuked-OPLL | **GPL-2.0** |
| `"PSG"` | YM7101 (DCSG) | Nuked-PSG | **GPL-2.0** |

未知の名前を渡すと `FM_ERR_UNKNOWN_CHIP` を返します。

> **ライセンスについて**: 各 Nuked コアの著作権は [Nuke.YKT](https://github.com/nukeykt) 氏にあります。  
> Nuked-OPLL と Nuked-PSG が GPL-2.0 であるため、それらを組み込む本プロジェクト全体を  
> **GNU General Public License v2.0** で配布します。  
> GPL-2.0 非対応の LGPL-2.1 コア (OPL2/OPL3/OPN2/OPM) のみを使用する場合でも、  
> DLL 全体としては GPL-2.0 が適用されます。

## ディレクトリ構成

```
NukedEngine/
├── include/
│   └── NukedEngineApi.h       ← 公開ヘッダー (FmEngineApi.h 互換)
├── src/
│   ├── NukedEngineApi.cpp     ← 実装
│   └── NukedEngineApi.def     ← MSVC エクスポート定義 (FmEngineApi.def と同一シンボル)
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

# ビルド (Linux / macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 成果物
#   build/bin/libNukedEngineApi.so
#   build/lib/libNukedEngineApi_s.a
```

## FMEngineTest での使い方

[FMEngineTest](https://github.com/madscient/FMEngineTest) は `FmEngineApi` 準拠エンジンを  
DLL として動的ロードしてテスト再生するツールです。  
`NukedEngineApi.dll` を `-e` で指定するだけで使用できます。

```bash
# FMEngineTest.exe と NukedEngineApi.dll を同じディレクトリに配置して:
FMEngineTest.exe -e NukedEngineApi.dll
FMEngineTest.exe -e NukedEngineApi.dll patches/opm.json
FMEngineTest.exe -e NukedEngineApi.dll patches/opm.json patches/opll.json
```

起動すると NukedEngine が対応するチップ一覧を表示し、JSON で指定されたチップを順番に発音します。

```
FMEngineTest
Loading engine: NukedEngineApi.dll
Engine loaded.

Sample rate: 48000 Hz

Supported chips (14): OPL2 OPL3 OPN2 OPN2C OPM OPP OPLL OPLL-B OPLLP OPLLP-B OPLL2 OPLLX VRC7 PSG

...
```

JSON の `"chips"` オブジェクトのキーがチップ名です。上記の対応チップ一覧にない名前  
(`"OPNA"`, `"OPL4"` 等) は `FM_ERR_UNKNOWN_CHIP` でスキップされます。

## アプリケーションへの組み込み

公開ヘッダー `NukedEngineApi.h` のみを include してください。  
DLL はオーディオ出力機能を持ちません。アプリケーションが任意のオーディオ API  
(RtAudio / WASAPI / ALSA / CoreAudio 等) を使って `FmEngine_Generate()` を呼び出し、  
得られた float32 の波形データをデバイスに送出します。

```c
#include "NukedEngineApi.h"

// エンジン作成 (デバイスのサンプルレートに合わせる)
FmEngineHandle eng = FmEngine_Create(48000);

// 対応チップを列挙
uint32_t n = FmEngine_Inquiry(eng);
for (uint32_t i = 0; i < n; ++i)
    printf("%s\n", FmEngine_GetSupportedChip(eng, i));

// チップを追加 (オーディオストリーム開始前に全て追加すること)
uint32_t opl3_id;
FmEngine_AddChip(eng, "OPL3", 0, &opl3_id);   // 文字列で指定
FmEngine_SetGain(eng, opl3_id, 1.0f, 1.0f);

// 複数チップの同時追加も可能
uint32_t opm_id;
FmEngine_AddChip(eng, "OPM", 0, &opm_id);

// レジスタ書き込み (任意スレッドからスレッドセーフ)
FmEngine_Write(eng, opl3_id, 0x05, 0x01, 1); // OPL3 enable
FmEngine_Write(eng, opl3_id, 0xB0, 0x32, 0); // Key-On

// --- アプリのオーディオコールバック内で呼ぶ ---
// 全チップの波形を合算して out_l / out_r に書き込む (float32, [-1.0, 1.0])
float out_l[512], out_r[512];
FmEngine_Generate(eng, out_l, out_r, 512);

// 解放
FmEngine_Destroy(eng);
```

### 注意

`FmEngine_AddChip` はオーディオストリーム開始前に全て完了させてください  
（オーディオコールバックスレッドとの競合防止）。

## YMEngine との実装上の差異

| 機能 | YMEngine | NukedEngine |
|---|---|---|
| FM コア | ymfm (cycle-approximate) | Nuked (cycle-accurate) |
| リサンプリング | LinearResampler (線形補間) | 同等の LinearResampler を内蔵 |
| マルチチップ | 複数 AddChip → Generate で合算 | **同一** |
| スレッド安全性 | SPSC ライトキュー | **同一** |
| オーディオ出力 | DLL 自体は出力しない（アプリが担当） | **同一** |
| ソフトクリップ | `FmEngine::generate()` 内で適用 | `FmEngine_Generate()` では**非適用** |
| 外部メモリ (ADPCM/PCM) | ✅ | ❌ (`FM_ERR_UNAVAILABLE`) |
| 対応チップ数 | 多数 (ymfm 対応チップ全て) | 14 種 (Nuked コアが対応するもののみ) |

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
> パッチデータ (FMEngineTest の patches/*.json) は YMEngine (ymfm) 用のものをそのまま流用できる。

## ライセンス

Copyright (C) 2026 MadScient  
本プロジェクトは **GNU General Public License v2.0** の下で配布します。  
詳細は [LICENSE](LICENSE) を参照してください。

### 依存コアのライセンス

| コア | 著作権 | ライセンス |
|---|---|---|
| [Nuked-OPL2-Lite](https://github.com/nukeykt/Nuked-OPL2-Lite) | Copyright (C) Nuke.YKT | LGPL-2.1 |
| [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | Copyright (C) Nuke.YKT | LGPL-2.1 |
| [Nuked-OPN2](https://github.com/nukeykt/Nuked-OPN2) | Copyright (C) Nuke.YKT | LGPL-2.1 |
| [Nuked-OPM](https://github.com/nukeykt/Nuked-OPM) | Copyright (C) Nuke.YKT | LGPL-2.1 |
| [Nuked-OPLL](https://github.com/nukeykt/Nuked-OPLL) | Copyright (C) Nuke.YKT | **GPL-2.0** |
| [Nuked-PSG](https://github.com/nukeykt/Nuked-PSG) | Copyright (C) Nuke.YKT | **GPL-2.0** |
