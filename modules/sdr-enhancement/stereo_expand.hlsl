// Isolated reference compute kernel; offline compilation only, no GPU execution.
// Bind decoded linear BT.709 SDR SRV. Use an sRGB SRV to decode encoded SDR once.
// Linear float input uses a non-sRGB SRV. Do NOT PQ-encode this output blindly.
// HOST MUST validate finite settings: ReferenceWhiteNits >= FLT_MIN (normal float),
// PeakNits >= ReferenceWhiteNits, 0 <= Knee < 1, 0 <= Strength <= 1.
// PeakNits / ReferenceWhiteNits must be finite and <= 65504 (RGBA16F maximum).
// Host also validates SDR samples/straight alpha, image bounds, two array slices,
// optional mask binding, resource usage, synchronization and runtime color contract.
Texture2DArray<float4> Input : register(t0);
Texture2DArray<float> UiMask : register(t1);
RWTexture2DArray<float4> Output : register(u0); // host must allocate RGBA16F
cbuffer Settings : register(b0) {
  uint2 Extent;
  uint EyeCount; // exactly 2 in this sample
  uint HasUiMask;
  uint2 SourceOffset;
  uint SourceFirstLayer;
  uint OutputFirstLayer;
  float ReferenceWhiteNits;
  float PeakNits;
  float Knee;
  float Strength; // shared across both eyes; no independent eye auto-exposure
};
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  if (any(tid.xy >= Extent) || tid.z >= EyeCount || EyeCount != 2) return;
  uint3 src = uint3(tid.xy + SourceOffset, tid.z + SourceFirstLayer);
  uint3 dst = uint3(tid.xy, tid.z + OutputFirstLayer);
  float4 c = Input.Load(int4(src, 0));
  if (HasUiMask != 0 && UiMask.Load(int4(src, 0)) > 0.5) { Output[dst] = c; return; }
  float y = dot(c.rgb, float3(0.2126, 0.7152, 0.0722));
  float ratio = PeakNits / ReferenceWhiteNits;
  float t = saturate((y - Knee) / (1.0 - Knee));
  float s = t * t * (3.0 - 2.0 * t);
  float mapped = y + (ratio - 1.0) * Strength * s * s;
  float3 rgb = y > 0.0 ? c.rgb * (mapped / y) : c.rgb;
  rgb /= max(1.0, max(rgb.r, max(rgb.g, rgb.b)) / ratio);
  Output[dst] = float4(rgb, c.a); // relative linear; 1 = ReferenceWhiteNits
}
