# CLAUDE.md

AI 向けの作業メモ。人間向けの文書は `README.md`。

## 文書の置き場所

- `docs/CHANGELOG.md` — 開発経緯。方針の前提、見送った案、確認結果を書く

## FmEngineApi への追従

- 仕様は FMEngineTest の `docs/FmEngineApi.md`、参照実装は YMEngine の
  `src/FmEngineApi.h` / `src/FmEngineApi.def`
- 仕様が変わったら `src/NukedEngineApi.h`、`src/NukedEngineApi.def`、README の
  互換性の表と見比べる
- 公開ヘッダは `src/NukedEngineApi.h` の 1 つだけ（CMake の `PUBLIC_HEADER`）
