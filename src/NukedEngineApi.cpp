// NukedEngineApi.cpp
//
// YMEngine (FmEngineApi.cpp) 互換の C ファサード実装。
// バックエンドは Nuked シリーズエミュレーター。
//
// 依存コアヘッダは以下のパスに配置すること:
//   cores/opl3/opl3.h   opl3.c
//   cores/opl2/opl2.h   opl2.c
//   cores/opn2/ym3438.h ym3438.c
//   cores/opm/opm.h     opm.c
//   cores/opll/opll.h   opll.c
//   cores/psg/ympsg.h   ympsg.c

#define NUKEDENGINE_EXPORTS
#include "NukedEngineApi.h"

// Nuked コアヘッダ
#include "opl3.h"
#include "opl2.h"
#include "ym3438.h"
#include "opm.h"
#include "opll.h"
#include "ympsg.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmdeviceapi.h>
#  include <audioclient.h>
#  include <avrt.h>
#  pragma comment(lib, "avrt.lib")
#  pragma comment(lib, "ole32.lib")
#endif

// =========================================================
// 標準クロック定数 (FmChip.h の FmClock:: と同値)
// =========================================================
namespace NukedClock {
    constexpr uint32_t OPL2  = 3'579'545;
    constexpr uint32_t OPL3  = 14'318'180;
    constexpr uint32_t OPN2  = 7'670'453;
    constexpr uint32_t OPM   = 3'579'545;
    constexpr uint32_t OPLL  = 3'579'545;
    constexpr uint32_t PSG   = 3'579'545;
}

// =========================================================
// LinearResampler (FmChip.h の LinearResampler と同等)
// float ネイティブサンプル → float ターゲットサンプルへ線形補間
// =========================================================
class LinearResampler {
public:
    void setup(uint32_t src_rate, uint32_t dst_rate) {
        m_src = src_rate; m_dst = dst_rate;
        m_phase_inc = (static_cast<uint64_t>(src_rate) << 32) / dst_rate;
        m_phase = 0;
        m_prev[0] = m_prev[1] = m_cur[0] = m_cur[1] = 0.0f;
        m_work[0].clear(); m_work[1].clear();
    }
    bool passthrough() const { return m_src == m_dst; }

    // generate_fn(float* l, float* r, uint32_t n) を呼びながら
    // dst_samples 分を out_l/out_r に書き込む (上書き)
    template<typename Fn>
    void process(Fn&& gen, float* out_l, float* out_r, uint32_t dst_samples) {
        if (passthrough()) { gen(out_l, out_r, dst_samples); return; }
        uint32_t need = static_cast<uint32_t>(
            (static_cast<uint64_t>(dst_samples) * m_src) / m_dst) + 2;
        m_work[0].resize(need); m_work[1].resize(need);
        gen(m_work[0].data(), m_work[1].data(), need);
        uint32_t si = 0;
        for (uint32_t di = 0; di < dst_samples; ++di) {
            uint32_t ip = static_cast<uint32_t>(m_phase >> 32);
            float frac  = static_cast<float>(m_phase & 0xFFFFFFFFull) * (1.0f / 4294967296.0f);
            while (si <= ip && si < need) {
                m_prev[0] = m_cur[0]; m_prev[1] = m_cur[1];
                m_cur[0] = m_work[0][si]; m_cur[1] = m_work[1][si];
                ++si;
            }
            out_l[di] = m_prev[0] + (m_cur[0] - m_prev[0]) * frac;
            out_r[di] = m_prev[1] + (m_cur[1] - m_prev[1]) * frac;
            m_phase += m_phase_inc;
        }
        m_phase -= static_cast<uint64_t>(si) << 32;
    }
private:
    uint32_t m_src = 0, m_dst = 0;
    uint64_t m_phase_inc = 0, m_phase = 0;
    float m_prev[2]{}, m_cur[2]{};
    std::vector<float> m_work[2];
};

// =========================================================
// ChipSlot – 1チップ分の状態 + ゲイン + リサンプラー
// =========================================================
struct ChipSlot {
    FmChipType type;
    uint32_t   clock;
    uint32_t   native_rate;
    float      gain_l = 1.0f, gain_r = 1.0f;
    LinearResampler resampler;

    // --- 各コアのステート ---
    union {
        opl3_chip  opl3;
        opl2_chip  opl2;
        ym3438_t   opn2;
        opm_t      opm;
        opll_t     opll;
        ympsg_t    psg;
    } state{};

    // --- SPSC ライトキュー (FmEngine.h 参照) ---
    struct WriteEntry { uint8_t reg, value; uint32_t port; };
    static constexpr size_t QUEUE_CAP = 4096;
    WriteEntry queue[QUEUE_CAP]{};
    std::atomic<size_t> q_write{0}, q_read{0};

    void enqueue(uint8_t reg, uint8_t value, uint32_t port) {
        size_t w = q_write.load(std::memory_order_relaxed);
        size_t next = (w + 1) % QUEUE_CAP;
        if (next == q_read.load(std::memory_order_acquire)) return; // full, drop
        queue[w] = {reg, value, port};
        q_write.store(next, std::memory_order_release);
    }

    void flush_queue() {
        size_t r = q_read.load(std::memory_order_relaxed);
        size_t w = q_write.load(std::memory_order_acquire);
        while (r != w) {
            auto& e = queue[r];
            write_immediate(e.reg, e.value, e.port);
            r = (r + 1) % QUEUE_CAP;
        }
        q_read.store(r, std::memory_order_release);
    }

    void write_immediate(uint8_t reg, uint8_t value, uint32_t port) {
        switch (type) {
        case FM_CHIP_OPL3: {
            // OPL3: port=0→bank0, port=1→bank1
            uint16_t full = static_cast<uint16_t>((port & 1) << 8 | reg);
            OPL3_WriteReg(&state.opl3, full, value);
            break;
        }
        case FM_CHIP_OPL2:
            OPL2_WriteReg(&state.opl2, reg, value);
            break;
        case FM_CHIP_OPN2:
        case FM_CHIP_OPN2_YM3438:
            // YMEngineと同様 port=0:addr-bank0, port=1:data-bank0,
            //                  port=2:addr-bank1, port=3:data-bank1
            OPN2_Write(&state.opn2, port & 0x3, value);
            (void)reg;
            break;
        case FM_CHIP_OPM:
        case FM_CHIP_OPP:
            // port=0:addr, port=1:data (OPM_Write はそのまま転送)
            OPM_Write(&state.opm, port, value);
            (void)reg;
            break;
        case FM_CHIP_OPLL:
        case FM_CHIP_VRC7:
        case FM_CHIP_OPLL_YM2413B:
        case FM_CHIP_OPLL_YMF281:
        case FM_CHIP_OPLL_YMF281B:
        case FM_CHIP_OPLL_YM2420:
        case FM_CHIP_OPLL_YM2423:
            OPLL_Write(&state.opll, port, value);
            (void)reg;
            break;
        case FM_CHIP_PSG:
            YMPSG_Write(&state.psg, value);
            (void)reg; (void)port;
            break;
        }
    }

    // ネイティブレートで n フレーム生成 → float L/R (−1..+1)
    void generate_native(float* l, float* r, uint32_t n) {
        constexpr float S16 = 1.0f / 32768.0f;
        constexpr float S15 = 1.0f / 16384.0f; // OPN2 9-bit signed

        switch (type) {
        case FM_CHIP_OPL3:
            for (uint32_t i = 0; i < n; ++i) {
                int16_t buf[2];
                OPL3_GenerateResampled(&state.opl3, buf); // already at target rate
                l[i] = buf[0] * S16;
                r[i] = buf[1] * S16;
            }
            break;
        case FM_CHIP_OPL2:
            for (uint32_t i = 0; i < n; ++i) {
                int16_t s;
                OPL2_GenerateResampled(&state.opl2, &s);
                l[i] = r[i] = s * S16;
            }
            break;
        case FM_CHIP_OPN2:
        case FM_CHIP_OPN2_YM3438:
            // cycle-accurate: 4 clocks = 1 stereo frame (24 master clocks)
            for (uint32_t i = 0; i < n; ++i) {
                int16_t buf[2]{};
                for (int c = 0; c < 4; ++c)
                    OPN2_Clock(&state.opn2, buf);
                l[i] = buf[0] * S15;
                r[i] = buf[1] * S15;
            }
            break;
        case FM_CHIP_OPM:
        case FM_CHIP_OPP:
            // 64 clocks per frame at OPM native rate
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out[2]{};
                for (int c = 0; c < 64; ++c)
                    OPM_Clock(&state.opm, out, nullptr, nullptr, nullptr);
                constexpr float OS = 1.0f / 32768.0f;
                float fl = std::clamp(out[0], -32768, 32767) * OS;
                float fr = std::clamp(out[1], -32768, 32767) * OS;
                l[i] = fl; r[i] = fr;
            }
            break;
        case FM_CHIP_OPLL:
        case FM_CHIP_VRC7:
        case FM_CHIP_OPLL_YM2413B:
        case FM_CHIP_OPLL_YMF281:
        case FM_CHIP_OPLL_YMF281B:
        case FM_CHIP_OPLL_YM2420:
        case FM_CHIP_OPLL_YM2423:
            // 72 clocks per frame
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out[2]{};
                for (int c = 0; c < 72; ++c)
                    OPLL_Clock(&state.opll, out);
                float m = std::clamp(out[0] + out[1], -32768, 32767) * S16;
                l[i] = r[i] = m;
            }
            break;
        case FM_CHIP_PSG:
            for (uint32_t i = 0; i < n; ++i) {
                int32_t out;
                YMPSG_Generate(&state.psg, &out);
                l[i] = r[i] = std::clamp(out, -32768, 32767) * S16;
            }
            break;
        }
    }

    // ターゲットレートで n フレーム生成 (リサンプリング込み)
    void generate(float* l, float* r, uint32_t n) {
        flush_queue();
        // OPL2/OPL3 は GenerateResampled が内蔵リサンプラーを持つため
        // native rate = target rate として扱い passthrough になる
        resampler.process(
            [this](float* ll, float* rr, uint32_t nn){ generate_native(ll, rr, nn); },
            l, r, n);
    }

    const char* chip_name() const {
        switch (type) {
        case FM_CHIP_OPL2:         return "OPL2 (YM3812)";
        case FM_CHIP_OPL3:         return "OPL3 (YMF262)";
        case FM_CHIP_OPN2:         return "OPN2 (YM2612)";
        case FM_CHIP_OPN2_YM3438:  return "OPN2 (YM3438)";
        case FM_CHIP_OPM:          return "OPM (YM2151)";
        case FM_CHIP_OPP:          return "OPP (YM2164)";
        case FM_CHIP_OPLL:         return "OPLL (YM2413)";
        case FM_CHIP_VRC7:         return "VRC7 (DS1001)";
        case FM_CHIP_OPLL_YM2413B: return "OPLL (YM2413B)";
        case FM_CHIP_OPLL_YMF281:  return "OPLL (YMF281)";
        case FM_CHIP_OPLL_YMF281B: return "OPLL (YMF281B)";
        case FM_CHIP_OPLL_YM2420:  return "OPLL (YM2420)";
        case FM_CHIP_OPLL_YM2423:  return "OPLL (YM2423)";
        case FM_CHIP_PSG:          return "PSG (YM7101)";
        default:                   return "Unknown";
        }
    }
};

// =========================================================
// FmEngineOpaque – エンジン本体
// =========================================================
struct FmEngineOpaque {
    uint32_t sample_rate;
    std::vector<std::unique_ptr<ChipSlot>> chips;
    std::mutex chips_mutex; // AddChip 時のみロック; Generate は lock-free

    // ワークバッファ (Generate 内で使う一時領域)
    std::vector<float> tmp_l, tmp_r;

    FmEngineOpaque(uint32_t sr) : sample_rate(sr) {}
};

// =========================================================
// ファクトリヘルパー
// =========================================================
static uint32_t default_clock(FmChipType t) {
    switch (t) {
    case FM_CHIP_OPL2:         return NukedClock::OPL2;
    case FM_CHIP_OPL3:         return NukedClock::OPL3;
    case FM_CHIP_OPN2:
    case FM_CHIP_OPN2_YM3438:  return NukedClock::OPN2;
    case FM_CHIP_OPM:
    case FM_CHIP_OPP:          return NukedClock::OPM;
    case FM_CHIP_OPLL:
    case FM_CHIP_VRC7:
    case FM_CHIP_OPLL_YM2413B:
    case FM_CHIP_OPLL_YMF281:
    case FM_CHIP_OPLL_YMF281B:
    case FM_CHIP_OPLL_YM2420:
    case FM_CHIP_OPLL_YM2423:  return NukedClock::OPLL;
    case FM_CHIP_PSG:          return NukedClock::PSG;
    default:                   return 3'579'545;
    }
}

static uint32_t native_rate_for(FmChipType t, uint32_t clk, uint32_t target_sr) {
    // OPL2/OPL3 は内部リサンプラーを使うので target と同じにする
    switch (t) {
    case FM_CHIP_OPL2:
    case FM_CHIP_OPL3:
        return target_sr;
    case FM_CHIP_OPN2:
    case FM_CHIP_OPN2_YM3438:
        // 4 clocks per frame, 6 master clocks per clock ⇒ clk / 24
        return clk / 24;
    case FM_CHIP_OPM:
    case FM_CHIP_OPP:
        return clk / 64;
    case FM_CHIP_OPLL:
    case FM_CHIP_VRC7:
    case FM_CHIP_OPLL_YM2413B:
    case FM_CHIP_OPLL_YMF281:
    case FM_CHIP_OPLL_YMF281B:
    case FM_CHIP_OPLL_YM2420:
    case FM_CHIP_OPLL_YM2423:
        return clk / 72;
    case FM_CHIP_PSG:
        return target_sr; // PSG は 1 sample/call の Generate を使う
    default:
        return target_sr;
    }
}

static uint32_t opll_type_from(FmChipType t) {
    switch (t) {
    case FM_CHIP_VRC7:         return opll_type_ds1001;
    case FM_CHIP_OPLL_YM2413B: return opll_type_ym2413b;
    case FM_CHIP_OPLL_YMF281:  return opll_type_ymf281;
    case FM_CHIP_OPLL_YMF281B: return opll_type_ymf281b;
    case FM_CHIP_OPLL_YM2420:  return opll_type_ym2420;
    case FM_CHIP_OPLL_YM2423:  return opll_type_ym2423;
    default:                   return opll_type_ym2413;
    }
}

static std::unique_ptr<ChipSlot> make_chip(FmChipType type, uint32_t clock,
                                           uint32_t target_sr)
{
    auto s = std::make_unique<ChipSlot>();
    s->type  = type;
    s->clock = clock ? clock : default_clock(type);
    s->native_rate = native_rate_for(type, s->clock, target_sr);
    s->resampler.setup(s->native_rate, target_sr);

    switch (type) {
    case FM_CHIP_OPL3:
        OPL3_Reset(&s->state.opl3, target_sr);
        break;
    case FM_CHIP_OPL2:
        OPL2_Reset(&s->state.opl2, target_sr);
        break;
    case FM_CHIP_OPN2: {
        OPN2_SetChipType(ym3438_mode_ym2612);
        OPN2_Reset(&s->state.opn2);
        break;
    }
    case FM_CHIP_OPN2_YM3438:
        OPN2_SetChipType(0);
        OPN2_Reset(&s->state.opn2);
        break;
    case FM_CHIP_OPM:
        OPM_Reset(&s->state.opm, opm_flags_none);
        break;
    case FM_CHIP_OPP:
        OPM_Reset(&s->state.opm, opm_flags_ym2164);
        break;
    case FM_CHIP_OPLL:
    case FM_CHIP_VRC7:
    case FM_CHIP_OPLL_YM2413B:
    case FM_CHIP_OPLL_YMF281:
    case FM_CHIP_OPLL_YMF281B:
    case FM_CHIP_OPLL_YM2420:
    case FM_CHIP_OPLL_YM2423:
        OPLL_Reset(&s->state.opll, opll_type_from(type));
        break;
    case FM_CHIP_PSG:
        YMPSG_Init(&s->state.psg);
        YMPSG_SetIC(&s->state.psg, 1);
        YMPSG_SetIC(&s->state.psg, 0);
        break;
    }
    return s;
}

// =========================================================
// FmEngine C API 実装
// =========================================================
extern "C" {

FMENGINE_API FmEngineHandle FMENGINE_CALL
FmEngine_Create(uint32_t sample_rate)
{
    if (sample_rate == 0) return nullptr;
    try {
        return reinterpret_cast<FmEngineHandle>(new FmEngineOpaque(sample_rate));
    } catch (...) { return nullptr; }
}

FMENGINE_API void FMENGINE_CALL
FmEngine_Destroy(FmEngineHandle engine)
{
    delete reinterpret_cast<FmEngineOpaque*>(engine);
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_AddChip(FmEngineHandle engine, FmChipType type, uint32_t clock,
                 uint32_t* out_id)
{
    if (!engine || !out_id) return FM_ERR_INVALID_ARG;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);
    try {
        auto slot = make_chip(type, clock, eng->sample_rate);
        if (!slot) return FM_ERR_INVALID_ARG;
        std::lock_guard<std::mutex> lk(eng->chips_mutex);
        *out_id = static_cast<uint32_t>(eng->chips.size());
        eng->chips.push_back(std::move(slot));
        return FM_OK;
    } catch (...) { return FM_ERR_EXCEPTION; }
}

FMENGINE_API const char* FMENGINE_CALL
FmEngine_GetChipName(FmEngineHandle engine, uint32_t chip_id)
{
    if (!engine) return "";
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);
    if (chip_id >= eng->chips.size()) return "";
    return eng->chips[chip_id]->chip_name();
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetNativeRate(FmEngineHandle engine, uint32_t chip_id)
{
    if (!engine) return 0;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);
    if (chip_id >= eng->chips.size()) return 0;
    return eng->chips[chip_id]->native_rate;
}

FMENGINE_API uint32_t FMENGINE_CALL
FmEngine_GetSampleRate(FmEngineHandle engine)
{
    if (!engine) return 0;
    return reinterpret_cast<FmEngineOpaque*>(engine)->sample_rate;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Write(FmEngineHandle engine, uint32_t chip_id,
               uint8_t reg, uint8_t value, uint32_t port)
{
    if (!engine) return FM_ERR_INVALID_ARG;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);
    if (chip_id >= eng->chips.size()) return FM_ERR_INVALID_ARG;
    eng->chips[chip_id]->enqueue(reg, value, port);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_SetGain(FmEngineHandle engine, uint32_t chip_id,
                 float gain_l, float gain_r)
{
    if (!engine) return FM_ERR_INVALID_ARG;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);
    if (chip_id >= eng->chips.size()) return FM_ERR_INVALID_ARG;
    eng->chips[chip_id]->gain_l = gain_l;
    eng->chips[chip_id]->gain_r = gain_r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_GetGain(FmEngineHandle engine, uint32_t chip_id,
                 float* out_gain_l, float* out_gain_r)
{
    if (!engine || !out_gain_l || !out_gain_r) return FM_ERR_INVALID_ARG;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);
    if (chip_id >= eng->chips.size()) return FM_ERR_INVALID_ARG;
    *out_gain_l = eng->chips[chip_id]->gain_l;
    *out_gain_r = eng->chips[chip_id]->gain_r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
FmEngine_Generate(FmEngineHandle engine,
                  float* out_l, float* out_r, uint32_t samples)
{
    if (!engine || !out_l || !out_r || samples == 0) return FM_ERR_INVALID_ARG;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);

    // 出力バッファをゼロクリア
    std::fill(out_l, out_l + samples, 0.0f);
    std::fill(out_r, out_r + samples, 0.0f);

    eng->tmp_l.resize(samples);
    eng->tmp_r.resize(samples);

    for (auto& chip : eng->chips) {
        chip->generate(eng->tmp_l.data(), eng->tmp_r.data(), samples);
        const float gl = chip->gain_l, gr = chip->gain_r;
        for (uint32_t i = 0; i < samples; ++i) {
            out_l[i] += eng->tmp_l[i] * gl;
            out_r[i] += eng->tmp_r[i] * gr;
        }
    }
    return FM_OK;
}

// =========================================================
// WASAPI API
// =========================================================

#ifdef _WIN32

struct WasapiOpaque {
    FmEngineOpaque* engine = nullptr;
    bool exclusive = false;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioRenderClient*  render     = nullptr;
    HANDLE               event      = nullptr;
    HANDLE               thread     = nullptr;
    HANDLE               stop_event = nullptr;
    WAVEFORMATEX         wfx{};
    uint32_t             buffer_frames = 0;

    static DWORD WINAPI audio_thread(LPVOID param) {
        auto* self = reinterpret_cast<WasapiOpaque*>(param);
        DWORD task_idx = 0;
        HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);
        self->run();
        if (h) AvRevertMmThreadCharacteristics(h);
        return 0;
    }

    void run() {
        while (true) {
            HANDLE events[] = { stop_event, event };
            DWORD r = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0) break; // stop
            if (r != WAIT_OBJECT_0 + 1) continue;

            uint32_t padding = 0;
            if (!exclusive)
                client->GetCurrentPadding(&padding);
            uint32_t avail = buffer_frames - padding;
            if (avail == 0) continue;

            BYTE* buf = nullptr;
            if (FAILED(render->GetBuffer(avail, &buf))) continue;

            auto* out = reinterpret_cast<float*>(buf);
            std::vector<float> l(avail), r(avail);
            FmEngine_Generate(
                reinterpret_cast<FmEngineHandle>(engine),
                l.data(), r.data(), avail);
            for (uint32_t i = 0; i < avail; ++i) {
                out[i * 2 + 0] = l[i];
                out[i * 2 + 1] = r[i];
            }
            render->ReleaseBuffer(avail, 0);
        }
    }
};

FMENGINE_API WasapiHandle FMENGINE_CALL
Wasapi_Create(FmEngineHandle engine, int exclusive)
{
    if (!engine) return nullptr;
    auto* eng = reinterpret_cast<FmEngineOpaque*>(engine);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto* w = new WasapiOpaque();
    w->engine    = eng;
    w->exclusive = exclusive != 0;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(&w->enumerator)))) goto fail;
    if (FAILED(w->enumerator->GetDefaultAudioEndpoint(
                eRender, eConsole, &w->device))) goto fail;
    if (FAILED(w->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                nullptr, reinterpret_cast<void**>(&w->client)))) goto fail;

    {
        WAVEFORMATEX& f = w->wfx;
        f.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        f.nChannels       = 2;
        f.nSamplesPerSec  = eng->sample_rate;
        f.wBitsPerSample  = 32;
        f.nBlockAlign     = f.nChannels * f.wBitsPerSample / 8;
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        f.cbSize          = 0;

        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        REFERENCE_TIME period = 0;
        if (w->exclusive) {
            period = 100'000; // 10ms
            flags |= AUDCLNT_STREAMFLAGS_NOPERSIST;
        }
        HRESULT hr = w->client->Initialize(
            w->exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
            flags, period, period, &f, nullptr);
        if (FAILED(hr)) goto fail;
    }

    w->event      = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    w->stop_event = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
    if (!w->event || !w->stop_event) goto fail;
    w->client->SetEventHandle(w->event);
    w->client->GetBufferSize(&w->buffer_frames);
    w->client->GetService(__uuidof(IAudioRenderClient),
                          reinterpret_cast<void**>(&w->render));
    return reinterpret_cast<WasapiHandle>(w);

fail:
    Wasapi_Destroy(reinterpret_cast<WasapiHandle>(w));
    return nullptr;
}

FMENGINE_API void FMENGINE_CALL
Wasapi_Destroy(WasapiHandle wasapi)
{
    if (!wasapi) return;
    auto* w = reinterpret_cast<WasapiOpaque*>(wasapi);
    if (w->thread) {
        SetEvent(w->stop_event);
        WaitForSingleObject(w->thread, 5000);
        CloseHandle(w->thread);
    }
    if (w->render)     w->render->Release();
    if (w->client)     w->client->Release();
    if (w->device)     w->device->Release();
    if (w->enumerator) w->enumerator->Release();
    if (w->event)      CloseHandle(w->event);
    if (w->stop_event) CloseHandle(w->stop_event);
    delete w;
}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Start(WasapiHandle wasapi)
{
    if (!wasapi) return FM_ERR_INVALID_ARG;
    auto* w = reinterpret_cast<WasapiOpaque*>(wasapi);
    if (!w->client) return FM_ERR_AUDIO;
    ResetEvent(w->stop_event);
    w->thread = CreateThread(nullptr, 0, WasapiOpaque::audio_thread, w, 0, nullptr);
    if (!w->thread) return FM_ERR_AUDIO;
    if (FAILED(w->client->Start())) return FM_ERR_AUDIO;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Stop(WasapiHandle wasapi)
{
    if (!wasapi) return FM_ERR_INVALID_ARG;
    auto* w = reinterpret_cast<WasapiOpaque*>(wasapi);
    if (!w->client) return FM_ERR_AUDIO;
    w->client->Stop();
    if (w->thread) {
        SetEvent(w->stop_event);
        WaitForSingleObject(w->thread, 5000);
        CloseHandle(w->thread); w->thread = nullptr;
    }
    return FM_OK;
}

#else // non-Windows stub

FMENGINE_API WasapiHandle FMENGINE_CALL
Wasapi_Create(FmEngineHandle, int) { return nullptr; }

FMENGINE_API void FMENGINE_CALL
Wasapi_Destroy(WasapiHandle) {}

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Start(WasapiHandle) { return FM_ERR_AUDIO; }

FMENGINE_API FmResult FMENGINE_CALL
Wasapi_Stop(WasapiHandle) { return FM_ERR_AUDIO; }

#endif // _WIN32

} // extern "C"
