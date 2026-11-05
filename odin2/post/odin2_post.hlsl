// Gtavport — pós-processamento antes da apresentação (menu "Video": Anti-aliasing e Upscaler).
// Mesmos passes e parâmetros do BOTWRecompAndroid (runtime/nvn/vulkan/shaders.c, MIT), portados de
// GLSL para o HLSL que o renderer do upstream compila com o DXC:
//   FXAA      no estilo do caminho de qualidade do FXAA 3.11 (Timothy Lottes, NVIDIA): implementação própria.
//   SMAA 1x   SMAA.hlsl de referência (Jimenez et al., MIT: third_party/smaa/LICENSE.txt), preset High.
//   FSR 1     AMD FidelityFX Super Resolution 1 (MIT): EASU (upscale por bordas) + RCAS (nitidez).
//   SGSR 1    Snapdragon Game Super Resolution 1 (sgsr1_shader_mobile_edge_direction, Copyright (c) 2025,
//             Qualcomm Innovation Center, Inc., BSD-3-Clause), modo padrão.
//
// Layout próprio (odin2_post.cpp): heap de texturas no set 0, heap de samplers no set 1, índices e
// parâmetros por push constant. Cada passe desenha um triângulo de tela cheia num alvo do tamanho da saída.

struct PushConstants {
    float4 a;
    float4 b;
    uint t0, t1, t2;
    uint linear_sampler, point_sampler;
    uint3 pad;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;
Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space1);

#define T0 g_Textures[g_Push.t0]
#define T1 g_Textures[g_Push.t1]
#define T2 g_Textures[g_Push.t2]
#define LINEAR g_Samplers[g_Push.linear_sampler]
#define POINT g_Samplers[g_Push.point_sampler]

float4 VSPost(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}

// ---------------------------------------------------------------- FXAA (a = 1/tamanho, tamanho)
float FxaaLuma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float3 FxaaAt(float2 uv) { return T0.SampleLevel(LINEAR, uv, 0).rgb; }
float FxaaLa(float2 uv) { return FxaaLuma(FxaaAt(uv)); }
static const float kFxaaSteps[10] = {1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0};

float4 PSFxaa(float4 pos : SV_Position) : SV_Target {
    float2 px = g_Push.a.xy, uv = pos.xy * px;
    float3 cM = FxaaAt(uv);
    float lM = FxaaLuma(cM), lN = FxaaLa(uv + float2(0.0, -px.y)), lS = FxaaLa(uv + float2(0.0, px.y));
    float lE = FxaaLa(uv + float2(px.x, 0.0)), lW = FxaaLa(uv + float2(-px.x, 0.0));
    float mx = max(lM, max(max(lN, lS), max(lE, lW))), mn = min(lM, min(min(lN, lS), min(lE, lW)));
    float range = mx - mn;
    if (range < max(0.0312, mx * 0.125)) return float4(cM, 1.0);
    float lNW = FxaaLa(uv - px), lSE = FxaaLa(uv + px);
    float lNE = FxaaLa(uv + float2(px.x, -px.y)), lSW = FxaaLa(uv + float2(-px.x, px.y));
    float sub = (2.0 * (lN + lS + lE + lW) + lNW + lNE + lSW + lSE) / 12.0;
    float subBlend = smoothstep(0.0, 1.0, saturate(abs(sub - lM) / range));
    subBlend = subBlend * subBlend * 0.75;
    float hor = 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE) + abs(lNW + lSW - 2.0 * lW);
    float ver = 2.0 * abs(lE + lW - 2.0 * lM) + abs(lNE + lNW - 2.0 * lN) + abs(lSE + lSW - 2.0 * lS);
    bool isH = hor >= ver;
    float pL = isH ? lS : lE, nL = isH ? lN : lW;
    float pG = abs(pL - lM), nG = abs(nL - lM);
    float stepLen = isH ? px.y : px.x, opp = pL, grad = pG;
    if (pG < nG) { stepLen = -stepLen; opp = nL; grad = nG; }
    float2 uvE = uv + (isH ? float2(0.0, stepLen * 0.5) : float2(stepLen * 0.5, 0.0));
    float2 es = isH ? float2(px.x, 0.0) : float2(0.0, px.y);
    float edgeL = (lM + opp) * 0.5, thr = grad * 0.25;
    float2 puv = uvE + es, nuv = uvE - es;
    float pD = FxaaLa(puv) - edgeL, nD = FxaaLa(nuv) - edgeL;
    bool pEnd = abs(pD) >= thr, nEnd = abs(nD) >= thr;
    [loop] for (int i = 1; i < 10 && !(pEnd && nEnd); i++) {
        if (!pEnd) { puv += es * kFxaaSteps[i]; pD = FxaaLa(puv) - edgeL; pEnd = abs(pD) >= thr; }
        if (!nEnd) { nuv -= es * kFxaaSteps[i]; nD = FxaaLa(nuv) - edgeL; nEnd = abs(nD) >= thr; }
    }
    float pDist = isH ? puv.x - uv.x : puv.y - uv.y, nDist = isH ? uv.x - nuv.x : uv.y - nuv.y;
    float shortest = min(pDist, nDist);
    bool sgn = pDist <= nDist ? pD >= 0.0 : nD >= 0.0;
    float edgeBlend = sgn == (lM - edgeL >= 0.0) ? 0.0 : 0.5 - shortest / (pDist + nDist);
    float f = max(subBlend, edgeBlend);
    return float4(FxaaAt(uv + (isH ? float2(0.0, stepLen * f) : float2(stepLen * f, 0.0))), 1.0);
}

// ---------------------------------------------------------------- SMAA 1x (a = SMAA_RT_METRICS)
#define SMAA_RT_METRICS g_Push.a
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL
#define SMAATexture2D(tex) Texture2D<float4> tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LINEAR, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(POINT, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LINEAR, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LINEAR, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(POINT, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LINEAR, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(LINEAR, coord, 0)
#include "third_party/smaa/SMAA.hlsl"

float2 PSSmaaEdge(float4 pos : SV_Position) : SV_Target {  // t0 = cor
    float2 uv = pos.xy * SMAA_RT_METRICS.xy;
    float4 off[3];
    SMAAEdgeDetectionVS(uv, off);
    return SMAALumaEdgeDetectionPS(uv, off, T0);
}

float4 PSSmaaWeight(float4 pos : SV_Position) : SV_Target {  // t0 = bordas, t1 = area, t2 = search
    float2 uv = pos.xy * SMAA_RT_METRICS.xy, pix;
    float4 off[3];
    SMAABlendingWeightCalculationVS(uv, pix, off);
    return SMAABlendingWeightCalculationPS(uv, pix, off, T0, T1, T2, float4(0.0, 0.0, 0.0, 0.0));
}

float4 PSSmaaBlend(float4 pos : SV_Position) : SV_Target {  // t0 = cor, t1 = pesos
    float2 uv = pos.xy * SMAA_RT_METRICS.xy;
    float4 off;
    SMAANeighborhoodBlendingVS(uv, off);
    return float4(SMAANeighborhoodBlendingPS(uv, off, T0, T1).rgb, 1.0);
}

// ---------------------------------------------------------------- FSR 1 EASU (a = entrada/saída xy, offset zw; b.xy = tamanho da entrada)
float3 EasuTap(int2 p) { return T0.Load(int3(clamp(p, int2(0, 0), int2(g_Push.b.xy) - 1), 0)).rgb; }
float EasuLum(float3 c) { return c.g + 0.5 * (c.r + c.b); }
void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float dx = lD - lB, dy = lE - lA;
    float lx = saturate(abs(dx) / max(max(abs(lD - lC), abs(lC - lB)), 1.0 / 65536.0));
    float ly = saturate(abs(dy) / max(max(abs(lE - lC), abs(lC - lA)), 1.0 / 65536.0));
    dir += float2(dx, dy) * w;
    len += (lx * lx + ly * ly) * w;
}
void EasuTapW(inout float3 ac, inout float aw, float2 off, float2 dir, float2 len2, float lob, float clp, float3 c) {
    float2 v = float2(dot(off, dir), dot(off, float2(-dir.y, dir.x))) * len2;
    float d2 = min(dot(v, v), clp);
    float wb = 0.4 * d2 - 1.0, wa = lob * d2 - 1.0;
    wb *= wb; wa *= wa;
    float w = (1.5625 * wb - 0.5625) * wa;
    ac += c * w; aw += w;
}

float4 PSEasu(float4 pos : SV_Position) : SV_Target {
    float2 pp = floor(pos.xy) * g_Push.a.xy + g_Push.a.zw;
    float2 fpf = floor(pp); pp -= fpf;
    int2 fp = int2(fpf);
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    float3 bC = EasuTap(fp + int2(0, -1)), cC = EasuTap(fp + int2(1, -1));
    float3 eC = EasuTap(fp + int2(-1, 0)), fC = EasuTap(fp), gC = EasuTap(fp + int2(1, 0)), hC = EasuTap(fp + int2(2, 0));
    float3 iC = EasuTap(fp + int2(-1, 1)), jC = EasuTap(fp + int2(0, 1)), kC = EasuTap(fp + int2(1, 1)), lC = EasuTap(fp + int2(2, 1));
    float3 nC = EasuTap(fp + int2(0, 2)), oC = EasuTap(fp + int2(1, 2));
    float bL = EasuLum(bC), cL = EasuLum(cC), eL = EasuLum(eC), fL = EasuLum(fC), gL = EasuLum(gC), hL = EasuLum(hC);
    float iL = EasuLum(iC), jL = EasuLum(jC), kL = EasuLum(kC), lL = EasuLum(lC), nL = EasuLum(nC), oL = EasuLum(oC);
    float2 dir = float2(0.0, 0.0);
    float len = 0.0;
    EasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    EasuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    EasuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    EasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    float dr = dot(dir, dir);
    bool zro = dr < 1.0 / 32768.0;
    dir = zro ? float2(1.0, 0.0) : dir * rsqrt(dr);
    len = len * 0.5; len *= len;
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 - 0.29 * len, clp = 1.0 / lob;
    float3 mn = min(min(fC, gC), min(jC, kC)), mx = max(max(fC, gC), max(jC, kC));
    float3 ac = float3(0.0, 0.0, 0.0);
    float aw = 0.0;
    EasuTapW(ac, aw, float2(0.0, -1.0) - pp, dir, len2, lob, clp, bC);
    EasuTapW(ac, aw, float2(1.0, -1.0) - pp, dir, len2, lob, clp, cC);
    EasuTapW(ac, aw, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, iC);
    EasuTapW(ac, aw, float2(0.0, 1.0) - pp, dir, len2, lob, clp, jC);
    EasuTapW(ac, aw, float2(0.0, 0.0) - pp, dir, len2, lob, clp, fC);
    EasuTapW(ac, aw, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, eC);
    EasuTapW(ac, aw, float2(1.0, 1.0) - pp, dir, len2, lob, clp, kC);
    EasuTapW(ac, aw, float2(2.0, 1.0) - pp, dir, len2, lob, clp, lC);
    EasuTapW(ac, aw, float2(2.0, 0.0) - pp, dir, len2, lob, clp, hC);
    EasuTapW(ac, aw, float2(1.0, 0.0) - pp, dir, len2, lob, clp, gC);
    EasuTapW(ac, aw, float2(1.0, 2.0) - pp, dir, len2, lob, clp, oC);
    EasuTapW(ac, aw, float2(0.0, 2.0) - pp, dir, len2, lob, clp, nC);
    // (pesos somando ~0 numa região saturada: NaN; usa o texel mais próximo, como no BOTW)
    return float4(aw > 1.0 / 65536.0 ? min(mx, max(mn, ac / aw)) : fC, 1.0);
}

// ---------------------------------------------------------------- FSR 1 RCAS (a.x = nitidez exp2(-stops); b.xy = tamanho)
float3 RcasTap(int2 p) { return T0.Load(int3(clamp(p, int2(0, 0), int2(g_Push.b.xy) - 1), 0)).rgb; }

float4 PSRcas(float4 pos : SV_Position) : SV_Target {
    int2 ip = int2(pos.xy);
    //    b
    //  d e f
    //    h
    float3 b = RcasTap(ip + int2(0, -1)), d = RcasTap(ip + int2(-1, 0)), e = RcasTap(ip), f = RcasTap(ip + int2(1, 0)),
           h = RcasTap(ip + int2(0, 1));
    float bL = EasuLum(b), dL = EasuLum(d), eL = EasuLum(e), fL = EasuLum(f), hL = EasuLum(h);
    float nz = 0.25 * (bL + dL + fL + hL) - eL;
    float rng = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);
    nz = 1.0 - 0.5 * saturate(abs(nz) / max(rng, 1.0 / 65536.0));
    float3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
    float3 hitMin = min(mn4, e) / max(4.0 * mx4, (1.0 / 65536.0).xxx);
    float3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, (-1.0 / 65536.0).xxx);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * g_Push.a.x * nz;
    return float4((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);
}

// ---------------------------------------------------------------- SGSR 1 (a = 1/entrada, entrada; b.xy = saída)
// Leituras por Load (centros de texel, clamp) em vez de Gather/sampler, como no BOTW: no Turnip o
// Gather deu imagem borrada e pontilhada num formato de 10 bits.
static const float kSgsrEdgeThreshold = 8.0 / 255.0, kSgsrEdgeSharpness = 2.0;
float4 SgsrTx(int2 p) { return T0.Load(int3(clamp(p, int2(0, 0), int2(g_Push.a.zw) - 1), 0)); }
float4 SgsrBilinear(float2 uv) {
    float2 x = uv * g_Push.a.zw - 0.5, f = frac(x);
    int2 i = int2(floor(x));
    return lerp(lerp(SgsrTx(i), SgsrTx(i + int2(1, 0)), f.x), lerp(SgsrTx(i + int2(0, 1)), SgsrTx(i + int2(1, 1)), f.x), f.y);
}
float4 SgsrGatherG(float2 uv) {
    int2 i = int2(floor(uv * g_Push.a.zw - 0.5));
    return float4(SgsrTx(i + int2(0, 1)).g, SgsrTx(i + int2(1, 1)).g, SgsrTx(i + int2(1, 0)).g, SgsrTx(i).g);
}
float SgsrLanczos2(float x) { float wA = x - 4.0; float wB = x * wA - wA; wA *= wA; return wB * wA; }
float2 SgsrWeightY(float dx, float dy, float c, float stdv) {
    float x = (dx * dx + dy * dy) * 0.55 + saturate(abs(c) * stdv);
    float w = SgsrLanczos2(x);
    return float2(w, w * c);
}

float4 PSSgsr1(float4 pos : SV_Position) : SV_Target {
    float4 vi = g_Push.a;
    float2 uv = pos.xy / g_Push.b.xy;
    float4 color = SgsrBilinear(uv);
    float2 imgCoord = uv * vi.zw + float2(-0.5, 0.5);
    float2 imgCoordPixel = floor(imgCoord);
    float2 coord = imgCoordPixel * vi.xy;
    float2 pl = imgCoord - imgCoordPixel;
    float4 left = SgsrGatherG(coord);
    float edgeVote = abs(left.z - left.y) + abs(color.y - left.y) + abs(color.y - left.z);
    [branch] if (edgeVote > kSgsrEdgeThreshold) {
        coord.x += vi.x;
        float4 right = SgsrGatherG(coord + float2(vi.x, 0.0));
        float4 upDown;
        upDown.xy = SgsrGatherG(coord + float2(0.0, -vi.y)).wz;
        upDown.zw = SgsrGatherG(coord + float2(0.0, vi.y)).yx;
        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= mean.xxxx; right -= mean.xxxx; upDown -= mean.xxxx;
        color.w = color.y - mean;
        float sum = abs(left.x) + abs(left.y) + abs(left.z) + abs(left.w) + abs(right.x) + abs(right.y) +
                    abs(right.z) + abs(right.w) + abs(upDown.x) + abs(upDown.y) + abs(upDown.z) + abs(upDown.w);
        float sumMean = 1.014185e+01 / sum;
        float stdv = sumMean * sumMean;
        float2 aWY = SgsrWeightY(pl.x, pl.y + 1.0, upDown.x, stdv);
        aWY += SgsrWeightY(pl.x - 1.0, pl.y + 1.0, upDown.y, stdv);
        aWY += SgsrWeightY(pl.x - 1.0, pl.y - 2.0, upDown.z, stdv);
        aWY += SgsrWeightY(pl.x, pl.y - 2.0, upDown.w, stdv);
        aWY += SgsrWeightY(pl.x + 1.0, pl.y - 1.0, left.x, stdv);
        aWY += SgsrWeightY(pl.x, pl.y - 1.0, left.y, stdv);
        aWY += SgsrWeightY(pl.x, pl.y, left.z, stdv);
        aWY += SgsrWeightY(pl.x + 1.0, pl.y, left.w, stdv);
        aWY += SgsrWeightY(pl.x - 1.0, pl.y - 1.0, right.x, stdv);
        aWY += SgsrWeightY(pl.x - 2.0, pl.y - 1.0, right.y, stdv);
        aWY += SgsrWeightY(pl.x - 2.0, pl.y, right.z, stdv);
        aWY += SgsrWeightY(pl.x - 1.0, pl.y, right.w, stdv);
        float finalY = aWY.y / aWY.x;
        float maxY = max(max(left.y, left.z), max(right.x, right.w));
        float minY = min(min(left.y, left.z), min(right.x, right.w));
        float deltaY = clamp(kSgsrEdgeSharpness * finalY, minY, maxY) - color.w;
        deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0);  // (entrada de alto contraste suavizada)
        color.xyz = saturate(color.xyz + deltaY);
    }
    return float4(color.xyz, 1.0);
}
