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
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <new>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <mmdeviceapi.h>
#  include <audioclient.h>
#  include <audiopolicy.h>
#  include <functiondiscoverykeys_devpkey.h>
#  include <wrl/client.h>
#  include <avrt.h>
#  pragma comment(lib, "avrt.lib")
#  pragma comment(lib, "ole32.lib")
   using Microsoft::WRL::ComPtr;
#endif

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

// FmChipType → NukedTag マッピング
static bool fmChipTypeToNukedTag(FmChipType t, NukedTag& out) {
    switch (t) {
        case FM_CHIP_OPL2:  out = NukedTag::OPL2;         return true;
        case FM_CHIP_OPL3:  out = NukedTag::OPL3;         return true;
        case FM_CHIP_OPN2:  out = NukedTag::OPN2_YM2612;  return true;
        case FM_CHIP_OPM:   out = NukedTag::OPM;          return true;
        case FM_CHIP_OPLL:  out = NukedTag::OPLL;         return true;
        case FM_CHIP_OPLLP: out = NukedTag::OPLL_YMF281;  return true;
        case FM_CHIP_OPLLX: out = NukedTag::OPLL_YM2423;  return true;
        case FM_CHIP_VRC7:  out = NukedTag::OPLL_VRC7;    return true;
        // Nuked コアが対応しないチップ
        case FM_CHIP_Y8950:
        case FM_CHIP_OPL:
        case FM_CHIP_OPL4:
        case FM_CHIP_OPN:
        case FM_CHIP_OPNA:
        case FM_CHIP_OPNB:
        case FM_CHIP_OPNBB:
        case FM_CHIP_OPZ:
        default: return false;
    }
}

static bool nukedChipTypeToTag(FmChipTypeNuked t, NukedTag& out) {
    switch (t) {
        case FM_NUKED_OPN2C:   out = NukedTag::OPN2C;     return true;
        case FM_NUKED_OPP:     out = NukedTag::OPP;       return true;
        case FM_NUKED_OPLLP_B: out = NukedTag::OPLLP_B;   return true;
        case FM_NUKED_OPLL2:   out = NukedTag::OPLL2;     return true;
        case FM_NUKED_OPLL_B:  out = NukedTag::OPLL_B;    return true;
        default: return false;
    }
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
        case NukedTag::OPN2C: return clk / 144;  // 6マスタークロック×24クロック=144
        case NukedTag::OPM:
        case NukedTag::OPP:     return clk / 64;
        case NukedTag::OPLL:
        case NukedTag::OPLL_B:
        case NukedTag::OPLL_YMF281:
        case NukedTag::OPLLP_B:
        case NukedTag::OPLL2:
        case NukedTag::OPLL_YM2423:
        case NukedTag::OPLL_VRC7:   return clk / 72;
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
            // OPL2/OPL3/PSG は WriteReg が即時完結
            // OPN2/OPM/OPLL は write_direct_full でフルサイクル処理
            // どちらも同一スレッド（WASAPIスレッド）から呼ばれるため競合なし
            write_direct_full(e.reg, e.value, e.port);
            t = (t + 1) % Q_CAP;
        }
        q_tail.store(t, std::memory_order_release);
    }


    // 全チップ共通の書き込み処理
    // OPL2/OPL3/PSG: WriteReg が即時完結
    // OPN2/OPM/OPLL: スロット/チャンネルタイミング依存のためフルサイクルのクロックを回す
    // generate() → flush() から WASAPIスレッドのみで呼ばれる（スレッド競合なし）
    void write_direct_full(uint8_t reg, uint8_t value, uint32_t port) {
        switch (tag) {
        case NukedTag::OPL3: {
            uint16_t full = static_cast<uint16_t>((port & 1u) << 8 | reg);
            OPL3_WriteReg(&state.opl3, full, value);
            break;
        }
        case NukedTag::OPL2:
            OPL2_WriteReg(&state.opl2, reg, value);
            break;
        case NukedTag::OPN2C: {
            int16_t dummy[2]{};
            state.opn2.write_data = (uint16_t)(((port & 0x02) << 7) | reg);
            state.opn2.write_a |= 1;
            for (int c = 0; c < 24; ++c) OPN2_Clock(&state.opn2, dummy);
            state.opn2.write_data = value;
            state.opn2.write_d |= 1;
            for (int c = 0; c < 24; ++c) OPN2_Clock(&state.opn2, dummy);
            break;
        }
        case NukedTag::OPM:
        case NukedTag::OPP: {
            int32_t dummy[2]{};
            state.opm.write_data = reg;
            state.opm.write_a = 1;
            for (int c = 0; c < 64; ++c) OPM_Clock(&state.opm, dummy, nullptr, nullptr, nullptr);
            state.opm.write_data = value;
            state.opm.write_d = 1;
            for (int c = 0; c < 64; ++c) OPM_Clock(&state.opm, dummy, nullptr, nullptr, nullptr);
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
            for (int c = 0; c < 72; ++c) OPLL_Clock(&state.opll, dummy);
            state.opll.write_data = value;
            state.opll.write_d |= 1;
            for (int c = 0; c < 72; ++c) OPLL_Clock(&state.opll, dummy);
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
        constexpr float S15 = 1.0f / 16384.0f;
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
            // 1サンプル = 24クロック (書き込みはwrite_direct_fullで処理済み)
            for (uint32_t i = 0; i < n; ++i) {
                int16_t buf[2]{};
                for (int c = 0; c < 24; ++c) OPN2_Clock(&state.opn2, buf);
                l[i] = buf[0] * S15; r[i] = buf[1] * S15;
            }
            break;
        case NukedTag::OPM:
        case NukedTag::OPP:
            // 1サンプル = 64クロック
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out[2]{};
                for (int c = 0; c < 64; ++c) OPM_Clock(&state.opm, out, nullptr, nullptr, nullptr);
                l[i] = std::clamp(out[0], -32768, 32767) * S16;
                r[i] = std::clamp(out[1], -32768, 32767) * S16;
            }
            break;
        case NukedTag::OPLL:
        case NukedTag::OPLL_B:
        case NukedTag::OPLL_YMF281:
        case NukedTag::OPLLP_B:
        case NukedTag::OPLL2:
        case NukedTag::OPLL_YM2423:
        case NukedTag::OPLL_VRC7:
            // 1サンプル = 72クロック
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out[2]{};
                for (int c = 0; c < 72; ++c) OPLL_Clock(&state.opll, out);
                float m = std::clamp(out[0] + out[1], -32768, 32767) * S16;
                l[i] = r[i] = m;
            }
            break;
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
    case NukedTag::OPM:  OPM_Reset(&s->state.opm, opm_flags_none);   break;
    case NukedTag::OPP:  OPM_Reset(&s->state.opm, opm_flags_ym2164); break;
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
    catch (const std::invalid_argument& e) {
        fprintf(stderr, "[NukedEngine] invalid_argument: %s\n", e.what());
        return FM_ERR_INVALID_ARG;
    }
    catch (const std::runtime_error& e) {
        fprintf(stderr, "[NukedEngine] runtime_error: %s\n", e.what());
        return FM_ERR_AUDIO;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[NukedEngine] exception: %s\n", e.what());
        return FM_ERR_EXCEPTION;
    }
    catch (...) {
        fprintf(stderr, "[NukedEngine] unknown exception\n");
        return FM_ERR_EXCEPTION;
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

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddChip(FmEngineHandle h, FmChipType api_type, uint32_t clock, uint32_t* out_id) {
    REQUIRE_PTR(h); REQUIRE_PTR(out_id);
    NukedTag tag;
    if (!fmChipTypeToNukedTag(api_type, tag)) return FM_ERR_INVALID_ARG;
    return safeCall([&]{
        auto* eng = static_cast<FmEngineOpaque*>(h);
        *out_id = eng->addSlot(makeSlot(tag, clock, eng->sample_rate));
    });
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddExtChip(FmEngineHandle h, FmChipTypeExt type,
                    uint32_t clock, uint32_t* out_id) {
    REQUIRE_PTR(h); REQUIRE_PTR(out_id);
    // FM_CHIP_EXT_DCSG のみ Nuked-PSG (YM7101) でサポート
    if (type == FM_CHIP_EXT_DCSG) {
        return safeCall([&]{
            auto* eng = static_cast<FmEngineOpaque*>(h);
            *out_id = eng->addSlot(makeSlot(NukedTag::PSG, clock, eng->sample_rate));
        });
    }
    // SSG / SCC / SAA は未サポート
    fprintf(stderr, "[NukedEngine] FmEngine_AddExtChip: type %d not supported\n",
            static_cast<int>(type));
    return FM_ERR_INVALID_ARG;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddNukedChip(FmEngineHandle h, FmChipTypeNuked type, uint32_t clock, uint32_t* out_id) {
    REQUIRE_PTR(h); REQUIRE_PTR(out_id);
    NukedTag tag;
    if (!nukedChipTypeToTag(type, tag)) return FM_ERR_INVALID_ARG;
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
    // 全チップ共通: キューに積んでWASAPIスレッドで処理
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
    return FM_ERR_INVALID_ARG;
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

// =========================================================
//  WASAPI API
// =========================================================
#ifdef _WIN32

// デバイス情報キャッシュ (Wasapi_GetDevice* 系)
struct DevInfo { std::wstring id, name; bool isDefault; };
static std::vector<DevInfo> s_deviceCache;

static std::vector<DevInfo> enumerateDevices() {
    std::vector<DevInfo> result;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = (hrCo == S_OK || hrCo == S_FALSE);
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))) {
        if (needUninit) CoUninitialize(); return result;
    }
    ComPtr<IMMDevice> pDef; std::wstring defId;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDef))) {
        LPWSTR p = nullptr;
        if (SUCCEEDED(pDef->GetId(&p)) && p) { defId = p; CoTaskMemFree(p); }
    }
    ComPtr<IMMDeviceCollection> col;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) {
        UINT cnt = 0; col->GetCount(&cnt);
        for (UINT i = 0; i < cnt; ++i) {
            ComPtr<IMMDevice> dev;
            if (FAILED(col->Item(i, &dev))) continue;
            DevInfo info; info.isDefault = false;
            LPWSTR p = nullptr;
            if (SUCCEEDED(dev->GetId(&p)) && p) { info.id = p; CoTaskMemFree(p); }
            ComPtr<IPropertyStore> props;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT v; PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v))
                    && v.vt == VT_LPWSTR && v.pwszVal) info.name = v.pwszVal;
                PropVariantClear(&v);
            }
            info.isDefault = (!defId.empty() && info.id == defId);
            result.push_back(std::move(info));
        }
    }
    if (needUninit) CoUninitialize();
    return result;
}

// -------------------------------------------------------
//  WasapiOpaque
// -------------------------------------------------------
struct WasapiOpaque {
    FmEngineOpaque* eng = nullptr;
    bool exclusive      = false;
    uint32_t sr         = 0;

    ComPtr<IMMDevice>          device;
    ComPtr<IAudioClient>       client;
    ComPtr<IAudioRenderClient> render;
    HANDLE ready_ev  = nullptr;
    HANDLE stop_ev   = nullptr;
    HANDLE thread_h  = nullptr;
    UINT32 buf_frames = 0;

    enum class Fmt { Float32, Int16, Int24, Int32, Unknown } dev_fmt = Fmt::Float32;
    UINT32 dev_channels   = 2;
    UINT32 dev_block_align = 8;

    std::vector<float> work_l, work_r;

    static DWORD WINAPI thread_proc(LPVOID p) {
        auto* self = static_cast<WasapiOpaque*>(p);
        DWORD idx = 0;
        HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &idx);
        self->render_loop();
        if (h) AvRevertMmThreadCharacteristics(h);
        return 0;
    }

    void render_loop() {
        HANDLE evs[2] = { ready_ev, stop_ev };
        while (true) {
            DWORD r = WaitForMultipleObjects(2, evs, FALSE, 200);
            if (r == WAIT_OBJECT_0 + 1 || r == WAIT_FAILED) break;
            if (r == WAIT_TIMEOUT) continue;
            UINT32 padding = 0;
            if (!exclusive) client->GetCurrentPadding(&padding);
            UINT32 avail = buf_frames - padding;
            if (avail == 0) continue;
            work_l.resize(avail); work_r.resize(avail);
            FmEngine_Generate(static_cast<FmEngineHandle>(eng),
                              work_l.data(), work_r.data(), avail);
            BYTE* buf = nullptr;
            if (FAILED(render->GetBuffer(avail, &buf))) break;
            write_frames(buf, avail);
            render->ReleaseBuffer(avail, 0);
        }
    }

    static float clamp1(float v) { return v<-1.f?-1.f:v>1.f?1.f:v; }

    void write_frames(BYTE* dst, UINT32 n) {
        switch (dev_fmt) {
        case Fmt::Float32: {
            auto* p = reinterpret_cast<float*>(dst);
            for (UINT32 i = 0; i < n; ++i) {
                p[i*dev_channels+0] = clamp1(work_l[i]);
                p[i*dev_channels+1] = clamp1(work_r[i]);
                for (UINT32 c = 2; c < dev_channels; ++c) p[i*dev_channels+c] = 0.f;
            }
            break;
        }
        case Fmt::Int16: {
            auto* p = reinterpret_cast<int16_t*>(dst);
            for (UINT32 i = 0; i < n; ++i) {
                p[i*dev_channels+0] = static_cast<int16_t>(clamp1(work_l[i]) * 32767.f);
                p[i*dev_channels+1] = static_cast<int16_t>(clamp1(work_r[i]) * 32767.f);
                for (UINT32 c = 2; c < dev_channels; ++c) p[i*dev_channels+c] = 0;
            }
            break;
        }
        default:
            memset(dst, 0, n * dev_block_align);
            break;
        }
    }

    bool init(FmEngineOpaque* e, bool excl, const wchar_t* device_id) {
        eng = e; exclusive = excl;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr != RPC_E_CHANGED_MODE && FAILED(hr)) return false;
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator)))) return false;
        if (device_id)
            enumerator->GetDevice(device_id, &device);
        else
            enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (!device) return false;
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(client.GetAddressOf()))))
            return false;
        WAVEFORMATEX* pFmt = nullptr;
        if (FAILED(client->GetMixFormat(&pFmt))) return false;
        dev_channels    = pFmt->nChannels;
        sr              = pFmt->nSamplesPerSec;
        dev_block_align = pFmt->nBlockAlign;
        // フォーマット判定
        if (pFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) dev_fmt = Fmt::Float32;
        else if (pFmt->wFormatTag == WAVE_FORMAT_PCM && pFmt->wBitsPerSample == 16) dev_fmt = Fmt::Int16;
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 20*10000, 0, pFmt, nullptr);
        CoTaskMemFree(pFmt);
        if (FAILED(hr)) return false;
        client->GetBufferSize(&buf_frames);
        ready_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stop_ev  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        client->SetEventHandle(ready_ev);
        client->GetService(IID_PPV_ARGS(&render));
        return true;
    }

    void start() {
        ResetEvent(stop_ev);
        client->Start();
        thread_h = CreateThread(nullptr, 0, thread_proc, this, 0, nullptr);
    }

    void stop() {
        if (thread_h) {
            SetEvent(stop_ev);
            WaitForSingleObject(thread_h, 5000);
            CloseHandle(thread_h); thread_h = nullptr;
        }
        if (client) { client->Stop(); client->Reset(); }
    }

    ~WasapiOpaque() {
        stop();
        if (ready_ev) CloseHandle(ready_ev);
        if (stop_ev)  CloseHandle(stop_ev);
        CoUninitialize();
    }
};

FMENGINE_API WasapiHandle FMENGINE_CALL
Wasapi_Create(FmEngineHandle h, int exclusive) {
    if (!h) return nullptr;
    auto* w = new(std::nothrow) WasapiOpaque();
    if (!w) return nullptr;
    if (!w->init(static_cast<FmEngineOpaque*>(h), exclusive != 0, nullptr)) {
        delete w; return nullptr;
    }
    return reinterpret_cast<WasapiHandle>(w);
}

FMENGINE_API WasapiHandle FMENGINE_CALL
Wasapi_CreateWithDevice(FmEngineHandle h, int exclusive, const wchar_t* device_id) {
    if (!h || !device_id) return nullptr;
    auto* w = new(std::nothrow) WasapiOpaque();
    if (!w) return nullptr;
    if (!w->init(static_cast<FmEngineOpaque*>(h), exclusive != 0, device_id)) {
        delete w; return nullptr;
    }
    return reinterpret_cast<WasapiHandle>(w);
}

FMENGINE_API uint32_t FMENGINE_CALL
Wasapi_GetDeviceCount(void) {
    try { s_deviceCache = enumerateDevices(); } catch (...) { s_deviceCache.clear(); }
    return static_cast<uint32_t>(s_deviceCache.size());
}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_GetDeviceId(uint32_t index, wchar_t* buf, uint32_t buf_len) {
    if (!buf || index >= s_deviceCache.size()) return FM_ERR_INVALID_ARG;
    wcsncpy_s(buf, buf_len, s_deviceCache[index].id.c_str(), _TRUNCATE);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_GetDeviceName(uint32_t index, wchar_t* buf, uint32_t buf_len) {
    if (!buf || index >= s_deviceCache.size()) return FM_ERR_INVALID_ARG;
    wcsncpy_s(buf, buf_len, s_deviceCache[index].name.c_str(), _TRUNCATE);
    return FM_OK;
}

FMENGINE_API int FMENGINE_CALL
Wasapi_IsDefaultDevice(uint32_t index) {
    if (index >= s_deviceCache.size()) return 0;
    return s_deviceCache[index].isDefault ? 1 : 0;
}

FMENGINE_API void FMENGINE_CALL
Wasapi_Destroy(WasapiHandle h) {
    delete reinterpret_cast<WasapiOpaque*>(h);
}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Start(WasapiHandle h) {
    REQUIRE_PTR(h);
    return safeCall([&]{ reinterpret_cast<WasapiOpaque*>(h)->start(); });
}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Stop(WasapiHandle h) {
    REQUIRE_PTR(h);
    return safeCall([&]{ reinterpret_cast<WasapiOpaque*>(h)->stop(); });
}

FMENGINE_API uint32_t FMENGINE_CALL
Wasapi_GetSampleRate(WasapiHandle h) {
    if (!h) return 0;
    return reinterpret_cast<WasapiOpaque*>(h)->sr;
}

#else // non-Windows stubs

FMENGINE_API WasapiHandle FMENGINE_CALL Wasapi_Create(FmEngineHandle, int) { return nullptr; }
FMENGINE_API WasapiHandle FMENGINE_CALL Wasapi_CreateWithDevice(FmEngineHandle, int, const wchar_t*) { return nullptr; }
FMENGINE_API uint32_t     FMENGINE_CALL Wasapi_GetDeviceCount(void) { return 0; }
FMENGINE_API FmResult     FMENGINE_CALL Wasapi_GetDeviceId(uint32_t, wchar_t*, uint32_t) { return FM_ERR_AUDIO; }
FMENGINE_API FmResult     FMENGINE_CALL Wasapi_GetDeviceName(uint32_t, wchar_t*, uint32_t) { return FM_ERR_AUDIO; }
FMENGINE_API int          FMENGINE_CALL Wasapi_IsDefaultDevice(uint32_t) { return 0; }
FMENGINE_API void         FMENGINE_CALL Wasapi_Destroy(WasapiHandle) {}
FMENGINE_API FmResult     FMENGINE_CALL Wasapi_Start(WasapiHandle) { return FM_ERR_AUDIO; }
FMENGINE_API FmResult     FMENGINE_CALL Wasapi_Stop(WasapiHandle) { return FM_ERR_AUDIO; }
FMENGINE_API uint32_t     FMENGINE_CALL Wasapi_GetSampleRate(WasapiHandle) { return 0; }

#endif // _WIN32

} // extern "C"
