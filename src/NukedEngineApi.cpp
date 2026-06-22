// NukedEngineApi.cpp
// FmEngineApi.cpp (YMEngine) と互換の C ファサード実装。
// バックエンドを ymfm から Nuked シリーズエミュレーターに差し替える。
//
// 依存コアヘッダは以下のパスに配置すること (git submodule):
//   cores/opl3/opl3.h   opl3.c      (Nuked-OPL3)
//   cores/opl2/opl2.h   opl2.c      (Nuked-OPL2-Lite)
//   cores/opn2/ym3438.h ym3438.c    (Nuked-OPN2)
//   cores/opm/opm.h     opm.c       (Nuked-OPM)
//   cores/opll/opll.h   opll.c      (Nuked-OPLL)
//   cores/psg/ympsg.h   ympsg.c     (Nuked-PSG)

#define NUKEDENGINE_EXPORTS
#include "NukedEngineApi.h"

// Nuked コアヘッダ
// これらは C ヘッダだが extern "C" ガードを持たないため、
// C++ からインクルードする際に明示的に extern "C" で囲む必要がある。
extern "C" {
#include "opl3.h"
#include "opl2.h"
#include "ym3438.h"
#include "opm.h"
#include "opll.h"
#include "ympsg.h"
}

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <new>



// =========================================================
//  標準クロック定数 (FmChip.h の FmClock:: と同値)
// =========================================================
namespace NukedClock {
    constexpr uint32_t Y8950  = 3'579'545;
    constexpr uint32_t OPL    = 3'579'545;
    constexpr uint32_t OPL2   = 3'579'545;
    constexpr uint32_t OPL3   = 14'318'180;
    constexpr uint32_t OPL4   = 33'868'800;
    constexpr uint32_t OPN    = 3'993'600;
    constexpr uint32_t OPNA   = 7'987'200;
    constexpr uint32_t OPNB   = 8'000'000;
    constexpr uint32_t OPNBB  = 8'000'000;
    constexpr uint32_t OPN2   = 7'670'453;
    constexpr uint32_t OPM    = 3'579'545;
    constexpr uint32_t OPLL   = 3'579'545;
    constexpr uint32_t OPLLP  = 3'579'545;
    constexpr uint32_t OPLLX  = 3'579'545;
    constexpr uint32_t OPZ    = 3'579'545;
    constexpr uint32_t VRC7   = 3'579'545;
    constexpr uint32_t PSG    = 3'579'545;
}

// 各チップの 1サンプルあたりのクロック数（libvgm device_start より）
// OPN2: 6マスタークロック×24クロック = 144マスタークロック / サンプル
// OPM:  clock / 64 (libvgm: rate = clock / 64)
// OPLL: clock / 72 (libvgm: rate = clock / 72)
#define NUKED_CLOCKS_PER_SAMPLE_OPN2  24
// Nuked-OPMはsh2立ち下がり(32clk間隔)ごとにDAC出力が更新される。
// 64clk/sampleだと1サンプル中に2回sh2↓が来るため実効周波数が2倍になる。
// 128clk/sampleにしてsh2↓の2回に1回だけサンプルを取得することで正しい音程を得る。
// nativeRate = clk/128 = 27965Hz
#define NUKED_CLOCKS_PER_SAMPLE_OPM   128
#define NUKED_CLOCKS_PER_SAMPLE_OPLL  72

// =========================================================
//  LinearResampler (FmChip.h の実装と同等)
// =========================================================
class LinearResampler {
public:
    void setup(uint32_t src_rate, uint32_t dst_rate) {
        m_src = src_rate; m_dst = dst_rate;
        m_phase_inc = (static_cast<uint64_t>(src_rate) << 32) / dst_rate;
        m_phase = 0;
        m_work_l.clear(); m_work_r.clear();
    }
    bool passthrough() const { return m_src == m_dst; }

    template<typename Fn>
    void process(Fn&& gen, float* out_l, float* out_r, uint32_t dst_n) {
        if (passthrough()) { gen(out_l, out_r, dst_n); return; }
        const uint32_t phase_offset = static_cast<uint32_t>(m_phase >> 32);
        const uint32_t src_needed = phase_offset +
            static_cast<uint32_t>((static_cast<uint64_t>(dst_n) * m_src) / m_dst) + 2;
        m_work_l.resize(src_needed); m_work_r.resize(src_needed);
        gen(m_work_l.data(), m_work_r.data(), src_needed);
        for (uint32_t di = 0; di < dst_n; ++di) {
            const uint32_t i0 = static_cast<uint32_t>(m_phase >> 32);
            const float frac  = static_cast<float>(m_phase & 0xFFFFFFFFull) * (1.0f/4294967296.0f);
            const uint32_t i1 = (i0 + 1 < src_needed) ? i0 + 1 : src_needed - 1;
            const uint32_t ii0 = (i0 < src_needed) ? i0 : src_needed - 1;
            out_l[di] = m_work_l[ii0] + (m_work_l[i1] - m_work_l[ii0]) * frac;
            out_r[di] = m_work_r[ii0] + (m_work_r[i1] - m_work_r[ii0]) * frac;
            m_phase += m_phase_inc;
        }
        const uint32_t consumed = static_cast<uint32_t>(m_phase >> 32);
        m_phase -= static_cast<uint64_t>(consumed) << 32;
    }
private:
    uint32_t m_src = 0, m_dst = 0;
    uint64_t m_phase_inc = 0, m_phase = 0;
    std::vector<float> m_work_l, m_work_r;
};

// =========================================================
//  ChipSlot – 1チップ分の状態
// =========================================================

// チップ種別を内部で一意に識別するタグ
enum class NukedTag {
    OPL2, OPL3,
    OPN2_YM2612, OPN2C,
    OPM, OPP,
    OPLL, OPLL_B, OPLLP_B, OPLL2,
    OPLL_YMF281, OPLL_YM2423, OPLL_VRC7,
    PSG,
};

// =========================================================
//  チップ名文字列 → NukedTag マッピング
//  FmEngine_AddChip / FmEngine_Inquiry / FmEngine_GetSupportedChip で使用
// =========================================================
struct ChipEntry { const char* name; NukedTag tag; };
static constexpr ChipEntry kChipTable[] = {
    { "OPL2",    NukedTag::OPL2        },
    { "OPL3",    NukedTag::OPL3        },
    { "OPN2",    NukedTag::OPN2_YM2612 },
    { "OPN2C",   NukedTag::OPN2C       },
    { "OPM",     NukedTag::OPM         },
    { "OPP",     NukedTag::OPP         },
    { "OPLL",    NukedTag::OPLL        },
    { "OPLL-B",  NukedTag::OPLL_B      },
    { "OPLLP",   NukedTag::OPLL_YMF281 },
    { "OPLLP-B", NukedTag::OPLLP_B     },
    { "OPLL2",   NukedTag::OPLL2       },
    { "OPLLX",   NukedTag::OPLL_YM2423 },
    { "VRC7",    NukedTag::OPLL_VRC7   },
    { "PSG",     NukedTag::PSG         },
};
static constexpr size_t kChipTableSize = sizeof(kChipTable) / sizeof(kChipTable[0]);

static bool nameToNukedTag(const char* name, NukedTag& out) {
    if (!name) return false;
    for (size_t i = 0; i < kChipTableSize; ++i) {
        if (strcmp(kChipTable[i].name, name) == 0) {
            out = kChipTable[i].tag;
            return true;
        }
    }
    return false;
}

static uint32_t defaultClock(NukedTag t) {
    switch (t) {
        case NukedTag::OPL2:         return NukedClock::OPL2;
        case NukedTag::OPL3:         return NukedClock::OPL3;
        case NukedTag::OPN2_YM2612:
        case NukedTag::OPN2C:        return NukedClock::OPN2;
        case NukedTag::OPM:
        case NukedTag::OPP:          return NukedClock::OPM;
        case NukedTag::OPLL:
        case NukedTag::OPLL_B:
        case NukedTag::OPLL_YMF281:
        case NukedTag::OPLLP_B:
        case NukedTag::OPLL2:
        case NukedTag::OPLL_YM2423:
        case NukedTag::OPLL_VRC7:    return NukedClock::OPLL;
        case NukedTag::PSG:          return NukedClock::PSG;
        default:                     return 3'579'545;
    }
}

static uint32_t nativeRate(NukedTag t, uint32_t clk, uint32_t target_sr) {
    switch (t) {
        case NukedTag::OPL2:
        case NukedTag::OPL3:    return target_sr;  // 内蔵リサンプラー使用
        case NukedTag::OPN2_YM2612:
        case NukedTag::OPN2C: return clk / (NUKED_CLOCKS_PER_SAMPLE_OPN2 * 6); // 6マスタークロック×24クロック=144マスタークロック/サンプル
        case NukedTag::OPM:
        case NukedTag::OPP:
            // gen_nativeはsh2↓(32clk周期)が2回来るまでクロックを回して1サンプル生成する。
            // sh2↓2回分の波形を1サンプルとして間引くため、実効nativeRateはclk/128相当。
            return clk / NUKED_CLOCKS_PER_SAMPLE_OPM;
        case NukedTag::OPLL:
        case NukedTag::OPLL_B:
        case NukedTag::OPLL_YMF281:
        case NukedTag::OPLLP_B:
        case NukedTag::OPLL2:
        case NukedTag::OPLL_YM2423:
        case NukedTag::OPLL_VRC7:   return clk / NUKED_CLOCKS_PER_SAMPLE_OPLL;
        case NukedTag::PSG:     return target_sr;
        default:                return target_sr;
    }
}

static const char* chipName(NukedTag t) {
    switch (t) {
        case NukedTag::OPL2:        return "OPL2 (YM3812)";
        case NukedTag::OPL3:        return "OPL3 (YMF262)";
        case NukedTag::OPN2_YM2612: return "OPN2 (YM2612)";
        case NukedTag::OPN2C:       return "OPN2C (YM3438)";
        case NukedTag::OPM:         return "OPM (YM2151)";
        case NukedTag::OPP:         return "OPP (YM2164)";
        case NukedTag::OPLL:        return "OPLL (YM2413)";
        case NukedTag::OPLL_B:      return "OPLL-B (YM2413B)";
        case NukedTag::OPLL_YMF281: return "OPLLP (YMF281)";
        case NukedTag::OPLLP_B:     return "OPLLP-B (YMF281B)";
        case NukedTag::OPLL2:       return "OPLL2 (YM2420)";
        case NukedTag::OPLL_YM2423: return "OPLLX (YM2423)";
        case NukedTag::OPLL_VRC7:   return "VRC7 (DS1001)";
        case NukedTag::PSG:         return "PSG (YM7101)";
        default:                    return "Unknown";
    }
}

static uint32_t opllTypeFrom(NukedTag t) {
    switch (t) {
        case NukedTag::OPLL_B:      return opll_type_ym2413b;
        case NukedTag::OPLL_YMF281: return opll_type_ymf281;
        case NukedTag::OPLLP_B:     return opll_type_ymf281b;
        case NukedTag::OPLL2:       return opll_type_ym2420;
        case NukedTag::OPLL_YM2423: return opll_type_ym2423;
        case NukedTag::OPLL_VRC7:   return opll_type_ds1001;
        default:                    return opll_type_ym2413;
    }
}

// -------------------------------------------------------
//  ChipSlot
// -------------------------------------------------------
struct ChipSlot {
    NukedTag   tag;
    uint32_t   clock_hz;
    uint32_t   native_rate_hz;
    std::atomic<float> gain_l{1.0f};
    std::atomic<float> gain_r{1.0f};
    LinearResampler resampler;
    // write_direct_full でのフルサイクルクロック消費分をサンプル単位でカウント
    // gen_native 冒頭でこの分だけ無音を出力してリサンプラーの位相を保つ
    uint32_t skip_samples = 0;
    // OPM: sh2↓のエッジ検出状態（サンプル間をまたいで持続）
    uint8_t opm_prev_sh2 = 0;

    union {
        opl3_chip opl3;
        opl2_chip opl2;
        ym3438_t  opn2;
        opm_t     opm;
        opll_t    opll;
        ympsg_t   psg;
    } state{};

    // SPSC ライトキュー (FmEngine.h の SpscQueue と同方式)
    struct WriteCmd { uint8_t reg, value; uint32_t port; };
    static constexpr size_t Q_CAP = 4096;
    WriteCmd q_buf[Q_CAP]{};
    std::atomic<size_t> q_head{0}, q_tail{0};

    void enqueue(uint8_t reg, uint8_t value, uint32_t port) {
        size_t h = q_head.load(std::memory_order_relaxed);
        size_t next = (h + 1) % Q_CAP;
        if (next == q_tail.load(std::memory_order_acquire)) return; // full, drop
        q_buf[h] = {reg, value, port};
        q_head.store(next, std::memory_order_release);
    }

    void flush() {
        size_t t = q_tail.load(std::memory_order_relaxed);
        while (t != q_head.load(std::memory_order_acquire)) {
            auto& e = q_buf[t];
            write_direct_full(e.reg, e.value, e.port);
            t = (t + 1) % Q_CAP;
        }
        q_tail.store(t, std::memory_order_release);
    }

    // OPN2/OPM/OPLL: キュー内の全エントリをフルサイクルで処理しつつ出力バッファに反映
    // clocks_per_sample: 1サンプルあたりのクロック数

    // 1クロック分の書き込み処理（gen_native の各クロック前に呼ぶ）
    // clks_needed: アドレス確定に必要なクロック数（OPN2=1, OPM/OPLL=2）


    // 全チップ共通の書き込み処理
    // OPL2/OPL3/PSG: WriteReg が即時完結
    // OPN2/OPM/OPLL: スロット/チャンネルタイミング依存のためフルサイクルのクロックを回す
    // generate() → flush() からオーディオコールバックスレッドのみで呼ばれる（スレッド競合なし）
    void write_direct_full(uint8_t reg, uint8_t value, uint32_t port) {
        constexpr float S16 = 1.0f / 32768.0f;
        switch (tag) {
        case NukedTag::OPL3: {
            uint16_t full = static_cast<uint16_t>((port & 1u) << 8 | reg);
            OPL3_WriteReg(&state.opl3, full, value);
            break;
        }
        case NukedTag::OPL2:
            OPL2_WriteReg(&state.opl2, reg, value);
            break;
        case NukedTag::OPN2_YM2612:
        case NukedTag::OPN2C: {
            // YMEngine APIのport: 0=バンク0(CH1-3), 1=バンク1(CH4-6)
            // Nuked-OPN2のwrite_data bit8: 0=バンク0, 1=バンク1
            // port != 0 のときバンク1 (0x100) を設定
            int16_t dummy[2]{};
            const uint16_t bank_bit = (port != 0) ? 0x100 : 0x000;
            state.opn2.write_data = bank_bit | reg;
            state.opn2.write_a |= 1;
            for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPN2; ++c) OPN2_Clock(&state.opn2, dummy);
            state.opn2.write_data = value;
            state.opn2.write_d |= 1;
            for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPN2; ++c) OPN2_Clock(&state.opn2, dummy);
            break;
        }
        case NukedTag::OPM:
        case NukedTag::OPP: {
            // フルサイクル方式: 書き込みを全スロット/チャンネルに確定させる
            // 消費クロックをskip_samplesに換算してgen_nativeで無音スキップする
            int32_t dummy[2]{};
            uint8_t sh1 = 0, sh2 = 0;
            state.opm.write_data = reg;
            state.opm.write_a = 1;
            // アドレス確定: 64クロック（全32スロット×2を網羅）
            for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPM / 2; ++c)
                OPM_Clock(&state.opm, dummy, &sh1, &sh2, nullptr);
            state.opm.write_data = value;
            state.opm.write_d = 1;
            for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPM / 2; ++c)
                OPM_Clock(&state.opm, dummy, &sh1, &sh2, nullptr);
            // gen_nativeのwhileループが使うエッジ検出状態をここでも同期させる
            // (write_direct_fullでもOPMクロックを消費するため、sh2の最新値を反映する)
            opm_prev_sh2 = sh2;
            // 128クロック消費 ÷ nativeRate(clk/128)の128クロック/サンプル... ではなく
            // 実際は64クロック×2フェーズ=128クロックで、nativeRate=clk/128なので2サンプル分
            skip_samples += 2;
            break;
        }
        case NukedTag::OPLL:
        case NukedTag::OPLL_B:
        case NukedTag::OPLL_YMF281:
        case NukedTag::OPLLP_B:
        case NukedTag::OPLL2:
        case NukedTag::OPLL_YM2423:
        case NukedTag::OPLL_VRC7: {
            int32_t dummy[2]{};
            state.opll.write_data = reg;
            state.opll.write_a |= 1;
            for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPLL; ++c)
                OPLL_Clock(&state.opll, dummy);
            state.opll.write_data = value;
            state.opll.write_d |= 1;
            for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPLL; ++c)
                OPLL_Clock(&state.opll, dummy);
            // 72×2=144クロック消費。gen_nativeは18クロック/サンプルなので
            // 144/18=8サンプル分をskipする
            skip_samples += 8;
            break;
        }
        case NukedTag::PSG:
            YMPSG_Write(&state.psg, value);
            break;
        default:
            break;
        }
    }
    void gen_native(float* l, float* r, uint32_t n) {
        constexpr float S16 = 1.0f / 32768.0f;
        constexpr float S15 = 1.0f / 16384.0f; // 未使用になるが念のため残す
        // write_direct_full でのフルサイクル消費分をスキップ（無音で補完）
        // リサンプラーが要求する n サンプルは必ず全て埋める
        if (skip_samples > 0) {
            uint32_t skip = std::min(skip_samples, n);
            std::fill(l, l + skip, 0.0f);
            std::fill(r, r + skip, 0.0f);
            skip_samples -= skip;
            l += skip; r += skip; n -= skip;
        }
        if (n == 0) return;
        switch (tag) {
        case NukedTag::OPL3:
            for (uint32_t i = 0; i < n; ++i) {
                int16_t buf[2]; OPL3_GenerateResampled(&state.opl3, buf);
                l[i] = buf[0] * S16; r[i] = buf[1] * S16;
            }
            break;
        case NukedTag::OPL2:
            for (uint32_t i = 0; i < n; ++i) {
                int16_t s; OPL2_GenerateResampled(&state.opl2, &s);
                l[i] = r[i] = s * S16;
            }
            break;
        case NukedTag::OPN2_YM2612:
        case NukedTag::OPN2C:
            // OPN2_Clock はサイクル毎にMOL/MOR ピン状態を出力する
            // YM2612モード: out_en = (cycles & 3) == 3 → 24クロック中6回有効
            //               各有効クロックで異なるチャンネルの出力が出る
            // YM3438モード: out_en = (cycles & 3) != 0 → 24クロック中18回有効
            // 有効クロックの出力を累積して1サンプルとする
            for (uint32_t i = 0; i < n; ++i) {
                int32_t suml = 0, sumr = 0;
                for (int c = 0; c < NUKED_CLOCKS_PER_SAMPLE_OPN2; ++c) {
                    int16_t buf[2]{};
                    OPN2_Clock(&state.opn2, buf);
                    suml += buf[0];
                    sumr += buf[1];
                }
                // 6チャンネル分の出力を合算（各CH出力は4クロック中1クロックで有効）
                // 合計24クロックで各CHが最大1回 → 合算値をそのまま使用
                l[i] = std::clamp(suml, -32768, 32767) * S16;
                r[i] = std::clamp(sumr, -32768, 32767) * S16;
            }
            break;
        case NukedTag::OPM:
        case NukedTag::OPP: {
            // sh2↓（32クロック間隔）が2回来るたびに1サンプルを生成
            // nativeRate = clk/128 = 27965Hz
            // opm_prev_sh2をChipSlotメンバーとして持続させ
            // サンプル間をまたいで正確なエッジ検出を行う
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out_l = 0, out_r = 0;
                int sh2_count = 0;
                while (sh2_count < 2) {
                    int32_t buf[2]{};
                    uint8_t sh1 = 0, sh2 = 0;
                    OPM_Clock(&state.opm, buf, &sh1, &sh2, nullptr);
                    if (opm_prev_sh2 && !sh2) {
                        ++sh2_count;
                        out_l = buf[0]; // sh2↓ごとに更新、2回目が最終値
                        out_r = buf[1];
                    }
                    opm_prev_sh2 = sh2;
                }
                l[i] = std::clamp(out_l, -32768, 32767) * S16;
                r[i] = std::clamp(out_r, -32768, 32767) * S16;
            }
            break;
        }
        case NukedTag::OPLL:
        case NukedTag::OPLL_B:
        case NukedTag::OPLL_YMF281:
        case NukedTag::OPLLP_B:
        case NukedTag::OPLL2:
        case NukedTag::OPLL_YM2423:
        case NukedTag::OPLL_VRC7: {
            // Nuked-OPLLは18クロックサイクルの特定cyclesでのみ各チャンネルの
            // キャリア出力(DAC値)がbuffer[0](output_m)に現れる。実測マッピング:
            //   CH1=8, CH2=9, CH3=10, CH4=14, CH5=15, CH6=16, CH7=2, CH8=3, CH9=4
            // リズムモード(reg0x0E bit5)有効時、BD/SD/TOM/HH/Cymの出力は
            // buffer[1](output_r)に現れる。リズムOFF時のoutput_rはsign値(±1)の
            // みで実質無音のため、常時合算してもメロディ専用時に影響しない。
            // Nuked-OPLLの内部位相生成は4倍速で進むため、1サンプルあたり
            // 実際には18クロックだけ消費する(NUKED_CLOCKS_PER_SAMPLE_OPLL=72は
            // nativeRate計算上の値であり、ここでは使わない)。
            // YM2413は9bit DAC(振幅±256程度)のため32768/256=128倍のゲインが必要だが、
            // 最大9ch+リズムの合算となるため過大にならないよう正規化する。
            constexpr int OPLL_CLOCKS_PER_SAMPLE_ACTUAL = 18;
            constexpr float OPLL_SCALE = S16 * 128.0f / 9.0f;
            for (uint32_t i = 0; i < n; ++i) {
                int32_t sum = 0;
                for (int c = 0; c < OPLL_CLOCKS_PER_SAMPLE_ACTUAL; ++c) {
                    int32_t buf[2]{};
                    OPLL_Clock(&state.opll, buf);
                    int cy = state.opll.cycles;
                    bool is_carrier_cycle =
                        cy == 8 || cy == 9 || cy == 10 ||
                        cy == 14 || cy == 15 || cy == 16 ||
                        cy == 2 || cy == 3 || cy == 4;
                    if (is_carrier_cycle) {
                        sum += buf[0] - 1; // メロディch: オフセット1を除去して合算
                    }
                    sum += buf[1] - 1; // リズムch: 常時合算(OFF時はsign値のみで実質無音)
                }
                l[i] = r[i] = std::clamp((float)sum * OPLL_SCALE, -1.f, 1.f);
            }
            break;
        }
        case NukedTag::PSG:
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out; YMPSG_Generate(&state.psg, &out);
                l[i] = r[i] = std::clamp(out, -32768, 32767) * S16;
            }
            break;
        }
    }

    // ターゲットレートで n フレーム生成 (リサンプリング込み)
    void generate(float* l, float* r, uint32_t n) {
        flush();
        resampler.process([this](float* ll, float* rr, uint32_t nn){ gen_native(ll, rr, nn); },
                          l, r, n);
    }
};

// -------------------------------------------------------
//  ファクトリ
// -------------------------------------------------------
static std::unique_ptr<ChipSlot> makeSlot(NukedTag tag, uint32_t clock, uint32_t sr) {
    auto s = std::make_unique<ChipSlot>();
    s->tag         = tag;
    s->clock_hz    = clock ? clock : defaultClock(tag);
    s->native_rate_hz = nativeRate(tag, s->clock_hz, sr);
    s->resampler.setup(s->native_rate_hz, sr);

    switch (tag) {
    case NukedTag::OPL3: OPL3_Reset(&s->state.opl3, sr); break;
    case NukedTag::OPL2: OPL2_Reset(&s->state.opl2, sr); break;
    case NukedTag::OPN2_YM2612:
        OPN2_SetChipType(ym3438_mode_ym2612);
        OPN2_Reset(&s->state.opn2); break;
    case NukedTag::OPN2C:
        OPN2_SetChipType(0);
        OPN2_Reset(&s->state.opn2); break;
    case NukedTag::OPM:  OPM_Reset(&s->state.opm, opm_flags_none);
        s->opm_prev_sh2 = s->state.opm.dac_osh2;  break;
    case NukedTag::OPP:  OPM_Reset(&s->state.opm, opm_flags_ym2164);
        s->opm_prev_sh2 = s->state.opm.dac_osh2;  break;
    case NukedTag::OPLL:
    case NukedTag::OPLL_B:
    case NukedTag::OPLL_YMF281:
    case NukedTag::OPLLP_B:
    case NukedTag::OPLL2:
    case NukedTag::OPLL_YM2423:
    case NukedTag::OPLL_VRC7:
        OPLL_Reset(&s->state.opll, opllTypeFrom(tag)); break;
    case NukedTag::PSG:
        YMPSG_Init(&s->state.psg);
        YMPSG_SetIC(&s->state.psg, 1);
        YMPSG_SetIC(&s->state.psg, 0); break;
    }
    return s;
}

// =========================================================
//  FmEngineOpaque – エンジン本体
// =========================================================
struct FmEngineOpaque {
    uint32_t sample_rate;
    std::vector<std::unique_ptr<ChipSlot>> chips;
    std::mutex add_mutex;   // addChip 時のみロック
    std::vector<float> tmp_l, tmp_r;

    explicit FmEngineOpaque(uint32_t sr) : sample_rate(sr) {}

    uint32_t addSlot(std::unique_ptr<ChipSlot> s) {
        std::lock_guard<std::mutex> lk(add_mutex);
        uint32_t id = static_cast<uint32_t>(chips.size());
        chips.push_back(std::move(s));
        return id;
    }
};

// =========================================================
//  例外ヘルパー (FmEngineApi.cpp の safeCall と同方式)
// =========================================================
template<typename Fn>
static FmResult safeCall(Fn&& fn) noexcept {
    try { fn(); return FM_OK; }
    catch (const std::bad_alloc&) {
        fprintf(stderr, "[NukedEngine] out of memory\n");
        return FM_ERR_ALLOC;
    }
    catch (const std::invalid_argument& e) {
        fprintf(stderr, "[NukedEngine] invalid_argument: %s\n", e.what());
        return FM_ERR_INVALID_ARG;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[NukedEngine] exception: %s\n", e.what());
        return FM_ERR_INVALID_ARG;
    }
    catch (...) {
        fprintf(stderr, "[NukedEngine] unknown exception\n");
        return FM_ERR_INVALID_ARG;
    }
}

#define REQUIRE_PTR(p) do { if (!(p)) return FM_ERR_INVALID_ARG; } while(0)

// =========================================================
//  FmEngine C API 実装
// =========================================================
extern "C" {

FMENGINE_API FmEngineHandle FMENGINE_CALL
FmEngine_Create(uint32_t sample_rate) {
    if (sample_rate == 0) return nullptr;
    return new(std::nothrow) FmEngineOpaque(sample_rate);
}

FMENGINE_API void FMENGINE_CALL
FmEngine_Destroy(FmEngineHandle h) {
    delete static_cast<FmEngineOpaque*>(h);
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_Inquiry(FmEngineHandle /*h*/) {
    return static_cast<uint32_t>(kChipTableSize);
}

FMENGINE_API const char* FMENGINE_CALL
FmEngine_GetSupportedChip(FmEngineHandle /*h*/, uint32_t index) {
    if (index >= kChipTableSize) return nullptr;
    return kChipTable[index].name;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddChip(FmEngineHandle h, const char* name, uint32_t clock, uint32_t* out_id) {
    REQUIRE_PTR(h); REQUIRE_PTR(out_id);
    NukedTag tag;
    if (!nameToNukedTag(name, tag)) return FM_ERR_UNKNOWN_CHIP;
    return safeCall([&]{
        auto* eng = static_cast<FmEngineOpaque*>(h);
        *out_id = eng->addSlot(makeSlot(tag, clock, eng->sample_rate));
    });
}

FMENGINE_API const char* FMENGINE_CALL
FmEngine_GetChipName(FmEngineHandle h, uint32_t chip_id) {
    if (!h) return nullptr;
    auto* eng = static_cast<FmEngineOpaque*>(h);
    if (chip_id >= eng->chips.size()) return nullptr;
    return chipName(eng->chips[chip_id]->tag);
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetNativeRate(FmEngineHandle h, uint32_t chip_id) {
    if (!h) return 0;
    auto* eng = static_cast<FmEngineOpaque*>(h);
    if (chip_id >= eng->chips.size()) return 0;
    return eng->chips[chip_id]->native_rate_hz;
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetSampleRate(FmEngineHandle h) {
    if (!h) return 0;
    return static_cast<FmEngineOpaque*>(h)->sample_rate;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Write(FmEngineHandle h, uint32_t chip_id, uint8_t reg, uint8_t value, uint32_t port) {
    REQUIRE_PTR(h);
    auto* eng = static_cast<FmEngineOpaque*>(h);
    if (chip_id >= eng->chips.size()) return FM_ERR_INVALID_ARG;
    // 全チップ共通: キューに積んでオーディオコールバックスレッドで処理
    eng->chips[chip_id]->enqueue(reg, value, port);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetGain(FmEngineHandle h, uint32_t chip_id, float gain_l, float gain_r) {
    REQUIRE_PTR(h);
    auto* eng = static_cast<FmEngineOpaque*>(h);
    if (chip_id >= eng->chips.size()) return FM_ERR_INVALID_ARG;
    eng->chips[chip_id]->gain_l.store(gain_l, std::memory_order_relaxed);
    eng->chips[chip_id]->gain_r.store(gain_r, std::memory_order_relaxed);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_GetGain(FmEngineHandle h, uint32_t chip_id, float* out_l, float* out_r) {
    REQUIRE_PTR(h); REQUIRE_PTR(out_l); REQUIRE_PTR(out_r);
    auto* eng = static_cast<FmEngineOpaque*>(h);
    if (chip_id >= eng->chips.size()) return FM_ERR_INVALID_ARG;
    *out_l = eng->chips[chip_id]->gain_l.load(std::memory_order_relaxed);
    *out_r = eng->chips[chip_id]->gain_r.load(std::memory_order_relaxed);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetMemory(FmEngineHandle /*h*/, uint32_t /*chip_id*/,
                   FmMemoryType /*mem_type*/, const uint8_t* /*data*/, uint32_t /*size*/) {
    // Nuked コアは外部メモリ未サポート
    fprintf(stderr, "[NukedEngine] FmEngine_SetMemory: not supported\n");
    return FM_ERR_UNAVAILABLE;
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetMemorySize(FmEngineHandle /*h*/, uint32_t /*chip_id*/, FmMemoryType /*mem_type*/) {
    return 0;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Generate(FmEngineHandle h, float* out_l, float* out_r, uint32_t samples) {
    REQUIRE_PTR(h); REQUIRE_PTR(out_l); REQUIRE_PTR(out_r);
    if (samples == 0) return FM_OK;
    auto* eng = static_cast<FmEngineOpaque*>(h);
    std::fill(out_l, out_l + samples, 0.0f);
    std::fill(out_r, out_r + samples, 0.0f);
    eng->tmp_l.resize(samples); eng->tmp_r.resize(samples);
    for (auto& chip : eng->chips) {
        chip->generate(eng->tmp_l.data(), eng->tmp_r.data(), samples);
        const float gl = chip->gain_l.load(std::memory_order_relaxed);
        const float gr = chip->gain_r.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < samples; ++i) {
            out_l[i] += eng->tmp_l[i] * gl;
            out_r[i] += eng->tmp_r[i] * gr;
        }
    }
    return FM_OK;
}


} // extern "C"
