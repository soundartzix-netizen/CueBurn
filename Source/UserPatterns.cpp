// 波形 (波形エディタ): すべてのパターンの「動きのカーブ」と「フェーダー」を、点 + 区間ごとのカーブで持つ
// 決まった形のパターンは、編集するまでは数式 (patternD / patternGate) のまま鳴らす
#include "PluginProcessor.h"

namespace
{
using Shape = ScratchProcessor::Shape;
using Pt = juce::Point<float>;
constexpr int NP = ScratchProcessor::numPresetPatterns;
constexpr int TS = ScratchProcessor::userTableSize;
constexpr int GS = ScratchProcessor::userGateSegments;

float tableAt (const std::array<float, TS + 1>& t, float phi) noexcept
{
    phi -= std::floor (phi);
    const float x = phi * (float) TS;
    const int i = juce::jlimit (0, TS - 1, (int) x);
    const float f = x - (float) i;
    return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
}

int segmentAt (float phi) noexcept
{
    phi -= std::floor (phi);
    return juce::jlimit (0, GS - 1, (int) (phi * (float) GS));
}

// 点の数に合わせて、区間ごとのカーブの情報をそろえる
void fixSegments (Shape& u)
{
    const size_t n = u.points.size() > 0 ? u.points.size() - 1 : 0;
    u.tension.resize (n, 0.0f);
    u.curve.resize (n, (unsigned char) ScratchProcessor::curveAuto);
}

// 点を間引く (Ramer-Douglas-Peucker)。細かい揺れのあるパターン用
void simplify (const std::vector<Pt>& in, size_t a, size_t b, float eps, std::vector<bool>& keep)
{
    if (b <= a + 1)
        return;
    const Pt pa = in[a], pb = in[b];
    float worst = -1.0f;
    size_t idx = a;
    for (size_t i = a + 1; i < b; ++i)
    {
        const float t = (in[i].x - pa.x) / juce::jmax (1.0e-6f, pb.x - pa.x);
        const float d = std::abs (in[i].y - (pa.y + t * (pb.y - pa.y)));
        if (d > worst)
        {
            worst = d;
            idx = i;
        }
    }
    if (worst > eps)
    {
        keep[idx] = true;
        simplify (in, a, idx, eps, keep);
        simplify (in, idx, b, eps, keep);
    }
}

// 1つの区間 [a, b] に、一番近いカーブ (S字 / 片側) を当てはめる。誤差が大きければ、途中に点を足して分ける
struct Fit
{
    int type;
    float tension;
    float err;
    int worstIndex;
};

Fit fitSegment (const std::vector<float>& y, int a, int b)
{
    const float y0 = y[(size_t) a], y1 = y[(size_t) b];
    const float span = y1 - y0;
    if (std::abs (span) < 1.0e-4f)
    {
        float err = 0.0f;
        int wi = a;
        for (int i = a; i <= b; ++i)
            if (std::abs (y[(size_t) i] - y0) > err)
            {
                err = std::abs (y[(size_t) i] - y0);
                wi = i;
            }
        return { ScratchProcessor::curveSingle, 0.0f, err, wi };
    }
    Fit best { ScratchProcessor::curveS, 0.0f, 1.0e9f, a };
    const int stride = juce::jmax (1, (b - a) / 48);
    for (int type : { (int) ScratchProcessor::curveS, (int) ScratchProcessor::curveSingle })
        for (int k = -40; k <= 40; ++k)
        {
            const float t = (float) k / 40.0f;
            float err = 0.0f;
            int wi = a;
            for (int i = a; i <= b; i += stride)
            {
                const float s = (float) (i - a) / (float) (b - a);
                const float e = std::abs (y0 + span * ScratchProcessor::curveValue (type, t, s) - y[(size_t) i]);
                if (e > err)
                {
                    err = e;
                    wi = i;
                }
            }
            if (err < best.err)
                best = { type, t, err, wi };
        }
    return best;
}

Shape presetToShape (int preset)
{
    Shape u;
    constexpr int n = 1024;
    std::vector<float> y ((size_t) n + 1);
    for (int i = 0; i <= n; ++i)
        y[(size_t) i] = (i == 0 || i == n) ? 0.0f : ScratchProcessor::patternD (preset, (float) i / (float) n);

    // 区切り: 山・谷、止まっている区間の境目
    std::vector<float> d ((size_t) n);
    float maxS = 1.0e-6f;
    for (int i = 0; i < n; ++i)
    {
        d[(size_t) i] = y[(size_t) i + 1] - y[(size_t) i];
        maxS = juce::jmax (maxS, std::abs (d[(size_t) i]));
    }
    auto cls = [&] (int i) { return std::abs (d[(size_t) i]) < 0.03f * maxS ? 0 : (d[(size_t) i] > 0.0f ? 1 : -1); };
    std::vector<int> keys { 0 };
    for (int i = 1; i < n; ++i)
        if (cls (i) != cls (i - 1))
            keys.push_back (i);
    keys.push_back (n);

    if ((int) keys.size() > ScratchProcessor::maxUserPoints)
    {
        // 細かい揺れが多いパターン: 点を間引いて、なめらか(自動)でつなぐ
        std::vector<Pt> dense ((size_t) n + 1);
        for (int i = 0; i <= n; ++i)
            dense[(size_t) i] = { (float) i / (float) n, y[(size_t) i] };
        float eps = 0.01f;
        for (;;)
        {
            std::vector<bool> keep (dense.size(), false);
            keep.front() = keep.back() = true;
            simplify (dense, 0, dense.size() - 1, eps, keep);
            u.points.clear();
            for (size_t i = 0; i < dense.size(); ++i)
                if (keep[i])
                    u.points.push_back (dense[i]);
            if ((int) u.points.size() <= ScratchProcessor::maxUserPoints)
                break;
            eps *= 1.4f;
        }
        fixSegments (u);
    }
    else
    {
        // 区間ごとにカーブを当てはめる (合わなければ、途中に点を足して分ける)
        std::vector<int> pts;
        std::vector<Fit> fits;
        std::function<void (int, int)> fitRange = [&] (int a, int b)
        {
            const auto f = fitSegment (y, a, b);
            const int room = ScratchProcessor::maxUserPoints - (int) keys.size() - (int) pts.size();
            if (f.err > 0.012f && b - a > 16 && room > 0 && f.worstIndex > a + 4 && f.worstIndex < b - 4)
            {
                fitRange (a, f.worstIndex);
                fitRange (f.worstIndex, b);
                return;
            }
            pts.push_back (a);
            fits.push_back (f);
        };
        for (size_t k = 0; k + 1 < keys.size(); ++k)
            fitRange (keys[k], keys[k + 1]);
        for (size_t k = 0; k < pts.size(); ++k)
        {
            u.points.push_back ({ (float) pts[k] / (float) n, y[(size_t) pts[k]] });
            u.tension.push_back (fits[k].tension);
            u.curve.push_back ((unsigned char) fits[k].type);
        }
        u.points.push_back ({ 1.0f, 0.0f });
    }
    u.points.front().y = 0.0f;
    u.points.back().y = 0.0f;
    for (int k = 0; k < GS; ++k)
        u.gate[(size_t) k] = ScratchProcessor::patternGate (preset, ((float) k + 0.5f) / (float) GS);
    return u;
}

void buildTable (Shape& u)
{
    fixSegments (u);
    const auto& pts = u.points;
    const int n = (int) pts.size();
    if (n < 2)
    {
        u.table.fill (0.0f);
        return;
    }

    // 「なめらか(自動)」の区間用: 形を崩さない3次補間 (Fritsch-Carlson) の傾き
    std::vector<float> d ((size_t) n - 1), m ((size_t) n);
    for (int k = 0; k < n - 1; ++k)
        d[(size_t) k] = (pts[(size_t) k + 1].y - pts[(size_t) k].y) / juce::jmax (1.0e-6f, pts[(size_t) k + 1].x - pts[(size_t) k].x);
    m[0] = d[0];
    m[(size_t) n - 1] = d[(size_t) n - 2];
    for (int k = 1; k < n - 1; ++k)
        m[(size_t) k] = d[(size_t) k - 1] * d[(size_t) k] <= 0.0f ? 0.0f : 0.5f * (d[(size_t) k - 1] + d[(size_t) k]);
    for (int k = 0; k < n - 1; ++k)
    {
        const float dk = d[(size_t) k];
        if (std::abs (dk) < 1.0e-9f)
        {
            m[(size_t) k] = m[(size_t) k + 1] = 0.0f;
            continue;
        }
        float a = m[(size_t) k] / dk, b = m[(size_t) k + 1] / dk;
        if (a < 0.0f) { m[(size_t) k] = 0.0f; a = 0.0f; }
        if (b < 0.0f) { m[(size_t) k + 1] = 0.0f; b = 0.0f; }
        const float s = a * a + b * b;
        if (s > 9.0f)
        {
            const float t = 3.0f / std::sqrt (s);
            m[(size_t) k] = t * a * dk;
            m[(size_t) k + 1] = t * b * dk;
        }
    }

    int k = 0;
    for (int i = 0; i <= TS; ++i)
    {
        const float x = (float) i / (float) TS;
        while (k < n - 2 && x > pts[(size_t) k + 1].x)
            ++k;
        const float x0 = pts[(size_t) k].x, x1 = pts[(size_t) k + 1].x;
        const float h = juce::jmax (1.0e-6f, x1 - x0);
        const float s = juce::jlimit (0.0f, 1.0f, (x - x0) / h);
        const float y0 = pts[(size_t) k].y, y1 = pts[(size_t) k + 1].y;
        float y;
        if (u.curve[(size_t) k] == ScratchProcessor::curveAuto)
        {
            const float s2 = s * s, s3 = s2 * s;
            y = (2.0f * s3 - 3.0f * s2 + 1.0f) * y0 + (s3 - 2.0f * s2 + s) * h * m[(size_t) k]
                + (-2.0f * s3 + 3.0f * s2) * y1 + (s3 - s2) * h * m[(size_t) k + 1];
        }
        else
        {
            y = y0 + (y1 - y0) * ScratchProcessor::curveValue (u.curve[(size_t) k], u.tension[(size_t) k], s);
        }
        u.table[(size_t) i] = juce::jlimit (0.0f, 1.0f, y);
    }
    float ms = 1.0e-6f;
    for (int i = 0; i < TS; ++i)
        ms = juce::jmax (ms, std::abs (u.table[(size_t) i + 1] - u.table[(size_t) i]) * (float) TS);
    u.maxSlope = ms;
}

// 決まった形のパターンを点とカーブにしたもの (初めて使う時に一度だけ作る。メッセージスレッドから)
const Shape& factoryShape (int preset)
{
    static std::array<std::unique_ptr<Shape>, NP> cache;
    auto& c = cache[(size_t) juce::jlimit (0, NP - 1, preset)];
    if (c == nullptr)
    {
        c = std::make_unique<Shape> (presetToShape (juce::jlimit (0, NP - 1, preset)));
        buildTable (*c);
    }
    return *c;
}
}

//==============================================================================
float ScratchProcessor::curveValue (int type, float tension, float s) noexcept
{
    s = juce::jlimit (0.0f, 1.0f, s);
    tension = juce::jlimit (-1.0f, 1.0f, tension);
    if (type == curveSingle)
        return std::pow (s, std::exp (tension * 2.3f)); // 0 = 直線、マイナス = 早く動く、プラス = 遅れて動く
    // S字: 0 = ふつう (動き出しと止まりがなめらか)、プラス = 真ん中が鋭く速い、マイナス = 両端が速い
    const float k = 2.5f * std::exp (tension * 1.4f);
    return s < 0.5f ? 0.5f * std::pow (2.0f * s, k) : 1.0f - 0.5f * std::pow (2.0f - 2.0f * s, k);
}

void ScratchProcessor::rebuildShapeTable (int pattern)
{
    buildTable (shapes[(size_t) pattern]);
}

const ScratchProcessor::Shape& ScratchProcessor::getShape (int pattern) const
{
    pattern = juce::jlimit (0, numPatterns - 1, pattern);
    if (pattern < NP && ! presetEdited[(size_t) pattern])
        return factoryShape (pattern);
    return shapes[(size_t) pattern];
}

bool ScratchProcessor::isShapeEdited (int pattern) const noexcept
{
    return pattern >= NP || (pattern >= 0 && presetEdited[(size_t) pattern]);
}

void ScratchProcessor::makeEditable (int pattern)
{
    if (pattern < 0 || pattern >= NP || presetEdited[(size_t) pattern])
        return;
    shapes[(size_t) pattern] = factoryShape (pattern);
    presetEdited[(size_t) pattern] = true;
}

float ScratchProcessor::presetMaxSlope (int pattern) noexcept
{
    // 決まった形のパターンの、一番速い所 (初回だけ計算)
    static const std::array<float, numPresetPatterns> table = []
    {
        std::array<float, numPresetPatterns> t {};
        constexpr int n = 4000;
        for (int p = 0; p < numPresetPatterns; ++p)
        {
            float ms = 1.0e-6f;
            for (int i = 0; i < n; ++i)
                ms = juce::jmax (ms, std::abs (patternD (p, (float) (i + 1) / (float) n) - patternD (p, (float) i / (float) n)) * (float) n);
            t[(size_t) p] = ms;
        }
        return t;
    }();
    return table[(size_t) juce::jlimit (0, numPresetPatterns - 1, pattern)];
}

float ScratchProcessor::warpPhase (float phi, float apex, float rise, float fall) noexcept
{
    apex = juce::jlimit (0.02f, 0.98f, apex);
    phi = juce::jlimit (0.0f, 1.0f, phi);
    if (phi < apex)
        return apex * curveValue (curveSingle, rise, phi / apex);
    return apex + (1.0f - apex) * curveValue (curveSingle, fall, (phi - apex) / (1.0f - apex));
}

float ScratchProcessor::patternApex (int pattern) const
{
    float best = -1.0f, at = 0.5f;
    for (int i = 0; i < 512; ++i)
    {
        const float phi = (float) i / 512.0f;
        const float v = shapeD (pattern, phi);
        if (v > best + 1.0e-4f)
        {
            best = v;
            at = phi;
        }
    }
    return juce::jlimit (0.02f, 0.98f, at);
}

float ScratchProcessor::naturalEnd (const FillEvent& e) const
{
    const float apex = patternApex (e.pattern);
    float last = 0.0f;
    for (int k = 0; k < 200; ++k)
    {
        const float phi = ((float) k + 0.5f) / 200.0f;
        if (shapeGate (e.pattern, warpPhase (phi, apex, e.bendRise, e.bendFall)))
            last = (float) (k + 1) / 200.0f;
    }
    return last;
}

bool ScratchProcessor::rtFastWarped (int pattern, float phi, float cut, float apex, float rise, float fall) const noexcept
{
    switch (pattern)
    {
        case 0: case 2: case 5: case 10: case 11: cut *= 0.5f; break;
        default: break;
    }
    if (cut <= 0.001f)
        return true;
    constexpr float h = 0.002f;
    const float a = rtD (pattern, warpPhase (juce::jmax (0.0f, phi - h), apex, rise, fall));
    const float b = rtD (pattern, warpPhase (juce::jmin (1.0f, phi + h), apex, rise, fall));
    const float slope = std::abs (b - a) / (2.0f * h);
    const int p = juce::jlimit (0, numPatterns - 1, pattern);
    const float maxS = live->useShape[(size_t) p] ? live->shapeMaxSlope[(size_t) p] : presetMaxSlope (p);
    return slope >= cut * maxS;
}

bool ScratchProcessor::rtFast (int pattern, float phi, float cut) const noexcept
{
    // フェーダーを開けたまま動きで聴かせるパターン (ベイビー系) は、カットを半分だけ効かせる
    // (耳での聴き比べ: ブーンバップはカット 50% が良く、ベイビーだけだと 50% では短すぎて 25% が良い。2026-10)
    switch (pattern)
    {
        case 0: case 2: case 5: case 10: case 11: cut *= 0.5f; break;
        default: break;
    }
    if (cut <= 0.001f)
        return true;
    constexpr float h = 0.002f;
    const float slope = std::abs (rtD (pattern, phi + h) - rtD (pattern, phi - h)) / (2.0f * h);
    const int p = juce::jlimit (0, numPatterns - 1, pattern);
    const float maxS = live->useShape[(size_t) p] ? live->shapeMaxSlope[(size_t) p] : presetMaxSlope (p);
    return slope >= cut * maxS;
}

float ScratchProcessor::shapeD (int pattern, float phi) const noexcept
{
    if (! isShapeEdited (pattern))
        return patternD (pattern, phi);
    return tableAt (shapes[(size_t) juce::jlimit (0, numPatterns - 1, pattern)].table, phi);
}

bool ScratchProcessor::shapeGate (int pattern, float phi) const noexcept
{
    if (! isShapeEdited (pattern))
        return patternGate (pattern, phi);
    return shapes[(size_t) juce::jlimit (0, numPatterns - 1, pattern)].gate[(size_t) segmentAt (phi)];
}

float ScratchProcessor::rtD (int pattern, float phi) const noexcept
{
    const int p = juce::jlimit (0, numPatterns - 1, pattern);
    if (! live->useShape[(size_t) p])
        return patternD (p, phi);
    return tableAt (live->shapeTab[(size_t) p], phi);
}

bool ScratchProcessor::rtGate (int pattern, float phi) const noexcept
{
    const int p = juce::jlimit (0, numPatterns - 1, pattern);
    if (! live->useShape[(size_t) p])
        return patternGate (p, phi);
    return live->shapeGateTab[(size_t) p][(size_t) segmentAt (phi)];
}

//==============================================================================
void ScratchProcessor::beginUserEdit()
{
    shapeHistory.push_back ({ shapes, presetEdited });
    if (shapeHistory.size() > 30)
        shapeHistory.erase (shapeHistory.begin());
}

bool ScratchProcessor::backUser()
{
    if (shapeHistory.empty())
        return false;
    shapes = shapeHistory.back().shapes;
    presetEdited = shapeHistory.back().edited;
    shapeHistory.pop_back();
    for (int p = 0; p < numPatterns; ++p)
        rebuildShapeTable (p);
    publishPhrase();
    return true;
}

void ScratchProcessor::resetShape (int pattern)
{
    if (! validPattern (pattern))
        return;
    beginUserEdit();
    if (pattern < NP)
    {
        presetEdited[(size_t) pattern] = false; // 数式に戻る
        shapes[(size_t) pattern] = Shape();
    }
    else
    {
        static const int defaults[numUserPatterns] = { 0, 1, 3, 2 };
        shapes[(size_t) pattern] = factoryShape (defaults[pattern - NP]);
    }
    rebuildShapeTable (pattern);
    publishPhrase();
}

void ScratchProcessor::copyShape (int dstPattern, int srcPattern)
{
    if (! validPattern (dstPattern) || ! validPattern (srcPattern))
        return;
    beginUserEdit();
    shapes[(size_t) dstPattern] = getShape (srcPattern);
    if (dstPattern < NP)
        presetEdited[(size_t) dstPattern] = true;
    rebuildShapeTable (dstPattern);
    publishPhrase();
}

int ScratchProcessor::addShapePoint (int pattern, juce::Point<float> p)
{
    if (! validPattern (pattern))
        return -1;
    makeEditable (pattern);
    auto& u = shapes[(size_t) pattern];
    auto& pts = u.points;
    if ((int) pts.size() >= maxUserPoints || pts.size() < 2)
        return -1;
    p.x = juce::jlimit (0.005f, 0.995f, p.x);
    p.y = juce::jlimit (0.0f, 1.0f, p.y);
    size_t i = 1;
    while (i < pts.size() && pts[i].x < p.x)
        ++i;
    if (p.x - pts[i - 1].x < 0.004f || (i < pts.size() && pts[i].x - p.x < 0.004f))
        return -1; // すぐ隣に点がある
    pts.insert (pts.begin() + (std::ptrdiff_t) i, p);
    // 分けた区間は、元の区間のカーブを引き継ぐ
    fixSegments (u);
    u.tension.insert (u.tension.begin() + (std::ptrdiff_t) i - 1, u.tension[i - 1]);
    u.curve.insert (u.curve.begin() + (std::ptrdiff_t) i - 1, u.curve[i - 1]);
    u.tension.resize (pts.size() - 1);
    u.curve.resize (pts.size() - 1);
    rebuildShapeTable (pattern);
    publishPhrase();
    return (int) i;
}

void ScratchProcessor::setShapePoint (int pattern, int index, juce::Point<float> p)
{
    if (! validPattern (pattern))
        return;
    makeEditable (pattern);
    auto& pts = shapes[(size_t) pattern].points;
    if (index <= 0 || index >= (int) pts.size() - 1)
        return;
    const float lo = pts[(size_t) index - 1].x + 0.004f, hi = pts[(size_t) index + 1].x - 0.004f;
    pts[(size_t) index] = { juce::jlimit (lo, juce::jmax (lo, hi), p.x), juce::jlimit (0.0f, 1.0f, p.y) };
    rebuildShapeTable (pattern);
    publishPhrase();
}

void ScratchProcessor::removeShapePoint (int pattern, int index)
{
    if (! validPattern (pattern))
        return;
    makeEditable (pattern);
    auto& u = shapes[(size_t) pattern];
    if (index <= 0 || index >= (int) u.points.size() - 1)
        return;
    fixSegments (u);
    u.points.erase (u.points.begin() + index);
    u.tension.erase (u.tension.begin() + index); // 左の区間のカーブを残す
    u.curve.erase (u.curve.begin() + index);
    rebuildShapeTable (pattern);
    publishPhrase();
}

void ScratchProcessor::setShapeTension (int pattern, int segment, float tension)
{
    if (! validPattern (pattern))
        return;
    makeEditable (pattern);
    auto& u = shapes[(size_t) pattern];
    fixSegments (u);
    if (segment < 0 || segment >= (int) u.tension.size())
        return;
    u.tension[(size_t) segment] = juce::jlimit (-1.0f, 1.0f, tension);
    rebuildShapeTable (pattern);
    publishPhrase();
}

void ScratchProcessor::setShapeCurve (int pattern, int segment, int curveType)
{
    if (! validPattern (pattern))
        return;
    makeEditable (pattern);
    auto& u = shapes[(size_t) pattern];
    fixSegments (u);
    if (segment < 0 || segment >= (int) u.curve.size())
        return;
    u.curve[(size_t) segment] = (unsigned char) juce::jlimit (0, 2, curveType);
    rebuildShapeTable (pattern);
    publishPhrase();
}

void ScratchProcessor::setShapeGate (int pattern, int gateSegment, bool open)
{
    if (! validPattern (pattern) || gateSegment < 0 || gateSegment >= GS)
        return;
    makeEditable (pattern);
    auto& g = shapes[(size_t) pattern].gate;
    if (g[(size_t) gateSegment] == open)
        return;
    g[(size_t) gateSegment] = open;
    publishPhrase();
}
