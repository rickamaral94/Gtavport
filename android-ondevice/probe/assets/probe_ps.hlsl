cbuffer C : register(b0, space4) { float4 tint; uint2 mask64; };
Texture2D t : register(t0); SamplerState s : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  uint64_t m = (uint64_t(mask64.y) << 32) | mask64.x;
  float bit = (float)((m >> (uint)(uv.x * 63.0)) & 1ull);
  return t.Sample(s, uv) * tint * bit; }
