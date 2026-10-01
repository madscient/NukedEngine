# CHANGELOG

開発経緯の記録。現在の仕様は `README.md` を参照。

## FmEngineApi の改訂に追従する（部位ゲイン、FmEngine_SetMemoryEx）

従った仕様：FMEngineTest `e002890` の `docs/FmEngineApi.md`。参照実装は
YMEngine `8f81213`（部位ゲインまで）。`8f81213` 時点の YMEngine は
`FmEngine_SetMemoryEx` をまだエクスポートしていない。

### 決めたこと

利用者からは「追従する」とだけ指示を受けた。以下はこちらで決め、利用者と
個別には決めていない。

- 任意シンボル 4 つ（`FmEngine_SetPartGain` / `GetPartGain` / `GetPartMask` /
  `SetMemoryEx`）をすべてエクスポートする。`.def` は仕様の必須・任意シンボルを
  すべて載せる。理由：README が FmEngineApi とシンボルが一致すると謳ってきた。
  任意シンボルが必須に上がっても非準拠にならない
- 部位を持たせるチップ
  - OPL3：`FM_PART_OPL3_AB` / `FM_PART_OPL3_CD`。`OPL3_Generate4ChResampled` の
    A/B/C/D を使う
  - OPLL 系 7 型番すべて：`FM_PART_OPLL_MELODY` / `FM_PART_OPLL_RHYTHM`。
    仕様書の表に無い OPLL-B / OPLLP-B / OPLL2 にも持たせた。仕様書の「表に無い
    チップは部位を持たない」は、括弧書き「出力が1系統のもの」の説明として読んだ
  - 根拠：利用者から、OPLL 系はすべて同じピンアウトで、違いは内蔵音色 ROM だけ。
    Nuked-OPLL も YM2413B / YMF281B / YM2420 で `output_r` を `output_m` と別に出す
    （**確認済み**：`opll.c` の `OPLL_Channel` を読んだ）
  - FMEngineTest の仕様書の表にこの 3 型番を足すかは未決
- `FmEngine_SetMemoryEx` は常に `FM_ERR_INVALID_ARG`。仕様は「チップが持たない
  `mem_type`」を `FM_ERR_INVALID_ARG` とし、`FM_ERR_UNAVAILABLE` は RAM をその場で
  読み書きできない場合に限っている。`FmEngine_SetMemory` は従来どおり
  `FM_ERR_UNAVAILABLE`
- 部位ゲインが既定値のときの出力は変えない。OPLL の復号（`buffer[0]` をキャリア
  サイクルだけ、`buffer[1]` を全サイクル、それぞれ `-1` して合算）はそのままにし、
  2 つの和を部位に分けただけ

前提：NukedEngine のチップが外部メモリを持たないこと。OPNA / Y8950 / OPL4 などの
Nuked コアを足すときは `SetMemory` / `SetMemoryEx` を実装し直す。

### 見送った案

- `FmEngine_SetMemoryEx` をエクスポートしない。仕様上はどちらでも準拠する。理由：
  `.def` を仕様のシンボル一覧にそろえる方を取った
- OPLL の端子が無音時に出す ±1 の符号レベルを 0 として復号し、部位を 0 にしたときに
  完全な無音にする。理由：既定ゲインでの出力も変わる。今の復号は実機波形との比較で
  決めたもので、変えるなら比較し直しが要る

### 確認

**確認済み**（MSVC 2019 x64 Release、Ninja でビルドし、`FmEngine_*` を呼ぶハーネスで
48 kHz の出力を比べた）：

- ビルドは DLL・静的ライブラリとも通る。警告は変更前と同じ 3 件
- DLL のエクスポートは 18（`dumpbin /exports`）
- 部位ゲインが既定値のとき、変更前のビルドと出力がビット単位で一致する
  （OPL2、OPL3 の A/B のみ・C/D のみ・A/B/C/D・互換モード、OPLL のメロディのみ・
  リズムのみ・両方、VRC7、OPLL2 の 10 ケース）
- `GetPartMask` は 14 チップすべてで上の決定どおり。既定値（OPL3_CD だけ 0）、
  持たない部位・範囲外の部位・未知の chip_id・NULL ポインタで `FM_ERR_INVALID_ARG`、
  `SetMemoryEx` は `FM_ERR_INVALID_ARG`
- OPL3：C/D にだけ出すチャンネルは既定で無音、CD=1 で A/B のみのときと同じ波形。
  A/B/C/D に出すチャンネルは CD=1 で振幅が 2 倍
- OPLL：既定の出力 = メロディ部位 + リズム部位、L/R 別の部位ゲインが線形に効く、
  全部位 0.5 = チップのゲイン 0.5（いずれも差 1e-6 未満）
- OPLL の部位を 0 にしたときに残る、もう一方の端子の無音レベル：メロディのみの
  場面でリズム端子が 0〜−2 LSB、リズムのみの場面でメロディ端子が 0〜−6 LSB
  （1 LSB = 128/9/32768）。音の成分は漏れていない（実効値の比は 20 倍以上）
- VRC7 のリズム部位は −18 LSB の一定値だけ（Nuked-OPLL が DS1001 で `output_r` を
  常に 0 にし、復号が 18 サイクル分 `-1` するため）

**未検証**：FMEngineTest からの読み込み（FMEngineTest は部位ゲインも `SetMemoryEx` も
まだ使っていない）。

### 旧公開ヘッダの削除

`include/NukedEngineApi.h` は旧 API（`FmChipType` 列挙、`FmEngine_AddNukedChip`
など）のまま、CMake の `PUBLIC_HEADER` としてインストールされていた。DLL の実装が
使うのは `src/NukedEngineApi.h`。利用者の指示（誰も使っていないなら削除）で削除し、
`PUBLIC_HEADER` とインクルードパスを `src/NukedEngineApi.h` に向けた。

**確認済み**（検索した）：参照していたのはこのリポジトリの CMakeLists と README
だけ。手元の他のリポジトリでは、FMEngineTest の `engines/` に置かれたこの
リポジトリの複製だけで、FMEngineTest のスクリプトと `src/` は参照していない。
GitHub のコード検索ではこのリポジトリだけが当たり、フォークは無い。

**未確認**：インストール済みのヘッダを非公開の場所で使っている利用者がいるか。
