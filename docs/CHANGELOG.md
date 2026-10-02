# CHANGELOG

開発経緯の記録。現在の仕様は `README.md` を参照。

## OPL2 / OPL3 / DCSG で clock を反映する。DCSG を直し、チップ名を PSG から DCSG に変える

前の項の「未解決」をすべて直した。

### 決めたこと

利用者と決めた：

- OPL2 / OPL3 / PSG の clock 対応と PSG の修正を行う
- チップ名 `PSG` を `DCSG` に変える（FMEngineTest のパッチと同じ名前）。旧名 `PSG` は
  残さず、渡すと `FM_ERR_UNKNOWN_CHIP`。`FmEngine_GetChipName` は `DCSG (YM7101)`

こちらで決めた（利用者と個別には決めていない）：

- OPL2 / OPL3：`OPL2_Reset` / `OPL3_Reset` に「サンプルレート × 49,716 × (OPL2 は 72、
  OPL3 は 288) ÷ clock」を四捨五入して渡す。コアの内蔵リサンプラーはそのまま使う。
  上限は `samplerate << 10` が uint32_t に収まる値、下限はコアの `rateratio` が 0 に
  ならない値（0 だと生成ループが終わらない）で切る
- `FmEngine_GetNativeRate` は OPL2 で clock / 72、OPL3 で clock / 288、DCSG で
  clock / 16 を返す。以前は 3 つとも出力サンプルレートを返していた。仕様の
  「ネイティブサンプルレート」に合わせた。外から見える値が変わる
- DCSG：ネイティブレートを clock / 16（`YMPSG_Generate` 1 回分）とし、出力レートへは
  区間平均（`AreaResampler`）で変換する。出力 1 サンプルあたりのネイティブサンプル数は
  clock / 16 / サンプルレートを整数に丸めずに使う
- DCSG の書き込み：`YMPSG_Write` のあと 8 クロック回す。8 は Nuked-PSG の
  `YMPSG_WriteBuffered` が書き込み同士の間に空けるクロック数。回したクロックの分は
  出力に無音を挟まない（OPN2 / OPM / OPLL と違う）。1 書き込みあたり約 2.2 µs 先に進む
- DCSG の初期化：`YMPSG_Init` のあと 32 クロック回す。直後はリセットがチップ内部に
  残っており、その間の書き込みは値が化ける。以前の `YMPSG_SetIC(1)` /
  `YMPSG_SetIC(0)` はクロックを回さないので何もしていなかった

見送った案：

- OPL2 / OPL3 でコアの内蔵リサンプラーを使わず、`OPL3_Generate4Ch` を clock / 288 で
  回して `LinearResampler` に通す。理由：標準クロックでも出力が変わる。上の案なら
  標準クロックで出力が変わらない
- DCSG を `LinearResampler` に通す。理由：折り返しが区間平均より大きい（下の確認）
- 旧名 `PSG` を別名として残す。理由：利用者が「DCSG にする」を選んだ

### 確認

**確認済み**（MSVC 2019 x64 Release）：

- clock を 2 倍にすると、OPL2 / OPL3 / DCSG の音程がちょうど 2 倍になる。DCSG の
  トーン周期 254 は 440.40 Hz（理論値 3,579,545 ÷ 32 ÷ 254 = 440.40）。3 バイトを
  続けて書いても反映される
- `GetNativeRate` は OPL2（3,579,545）と OPL3（14,318,180）で 49,715、DCSG
  （3,579,545）で 223,721
- 標準クロックでは OPL2 / OPL3 / OPLL の出力が変更前とビット単位で一致する
  （部位ゲインの確認と同じ 10 ケース）。部位ゲインと clock=0 の確認 51 項目も通る
- FMEngineTest（`866f4a3` のソースをビルド）で WAV を書き出した。`dcsg.json` は
  CH0〜2 が 440.40 / 494.96 / 522.72 Hz（パッチのトーン周期からの理論値と一致）、
  ノイズも鳴る。変更前の DLL では `DCSG` が未知のチップとしてスキップされる。
  `opl2` `opl3` `opll` `opllp` `opllx` `vrc7` `opm` `opn2` の WAV は変更前の DLL と
  バイト単位で一致する
- 初期化直後のリセットは、`YMPSG_Init` から 26 クロックで抜ける（コア単体で数えた）。
  対策前は `dcsg.json` の CH0 だけ 466.09 Hz（トーン周期 240）で鳴っていた。
  リセット中に書いた音量バイト `0x90` の下位 4 ビットで、周期の下位 4 ビットが
  上書きされていた
- 区間平均の効果：ネイティブレートで作った理想的な矩形波（437 Hz）を 48 kHz に
  変換し、高調波以外の成分を高調波と比べた。線形補間で −22.8 dB、区間平均で
  −31.2 dB（Python で計算した。Nuked-PSG の出力そのものではない）

DCSG の出力は正の値だけで、直流を含む（Nuked-PSG の出力のまま）。手を入れていない。

## FmEngine_AddChip の clock=0（標準クロック）廃止に追従する

従った仕様：FMEngineTest `866f4a3` の `docs/FmEngineApi.md`。`FmEngine_AddChip` は
clock=0 を `FM_ERR_INVALID_ARG` で拒否し、エンジンは既定のクロックを持たない。

- 既定クロックの定数表と `defaultClock()` を削除した
- 判定の順序は、ポインタ → clock → チップ名。未知の名前で clock=0 なら
  `FM_ERR_INVALID_ARG` を返す。仕様は順序を定めておらず、こちらで決めた

FMEngineTest `c0589c1`（外部メモリの規則の明確化）も読んだ。変更は要らない。
`FmEngine_SetMemory` は `data` に触れない。`FmEngine_SetMemoryEx` のエクスポートは
「外部メモリのバスが外に出ているチップを扱うエンジンにお勧め」とされ、NukedEngine は
該当しないが、常に `FM_ERR_INVALID_ARG` を返すスタブのエクスポートは続ける
（仕様上どちらでも準拠する）。

### 確認

**確認済み**（MSVC 2019 x64 Release、`FmEngine_*` を呼ぶハーネス、48 kHz）：

- clock=0 で `FM_ERR_INVALID_ARG`、チップは追加されず `out_id` も書き換わらない。
  未知の名前は clock=0 なら `FM_ERR_INVALID_ARG`、clock を渡せば
  `FM_ERR_UNKNOWN_CHIP`
- 変更前の既定値と同じ clock を明示して鳴らした出力は、変更前に clock=0 で鳴らした
  出力とビット単位で一致する（部位ゲインの確認と同じ 10 ケース）
- clock を 2 倍にすると、OPN2 / OPN2C / OPM / OPP は音程がちょうど 2 倍になる。
  OPLL 系 7 型番も出力が変わる

### 未解決（この変更の前からある。直していない。次の項ですべて直した）

- OPL2 / OPL3 は clock を出力に反映しない。**確認済み**：clock を 2 倍にしても
  出力がビット単位で一致する。`OPL2_Reset` / `OPL3_Reset` にはサンプルレートしか
  渡しておらず、コアは内部レートを 49,716 Hz に固定している（コードを読んだ）。
  直す案：`Reset` に「サンプルレート × 49,716 × (OPL3 は 288、OPL2 は 72) ÷ clock」
  を渡す。標準クロックでは整数に丸めると元のサンプルレートと同じになる
  （**未検証**：計算しただけ）
- PSG は clock を出力に反映せず、出力サンプルレートの 16 倍のクロックで動く。
  **確認済み**：48 kHz でトーン周期 256 の音が 93.75 Hz（= 48,000 × 16 ÷ 32 ÷ 256）。
  clock が 3,579,545 なら 436.96 Hz のはずで、clock を 2 倍にしても 93.75 Hz のまま。
  `nativeRate()` が出力サンプルレートを返し、16 クロック進める `YMPSG_Generate` を
  出力 1 サンプルに 1 回呼んでいるため（コードを読んだ）
- PSG は、1 回の `FmEngine_Generate` の前に書いた値のうち最後の 1 バイトしか
  反映されない。**確認済み**：3 バイトをまとめて書くと無音、書き込みごとに
  1 サンプル生成を挟むと鳴る。`YMPSG_Write` は値を 1 つ保持して次のクロックで
  取り込むだけで、`flush()` がクロックを挟まずに続けて呼ぶため（コードを読んだ）
- チップ名 `PSG`（YM7101、SN76489 系の DCSG）は、FMEngineTest のパッチのチップ名
  （SN76489 系は `DCSG`、YM2149 は `SSG`）のどちらとも一致しない。FMEngineTest からは
  NukedEngine の PSG が鳴らされない

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
