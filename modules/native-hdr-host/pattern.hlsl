// Known source samples, not a claim of measured panel luminance.
// RGB source: linear BT.709 relative white. 1 = ReferenceWhiteNits.
// Final RGB: BT.2020 ST2084/PQ absolute nits. Alpha remains opaque.
cbuffer Pattern : register(b0) { float ReferenceWhiteNits; float PeakNits; float2 Padding; };
struct Vertex { float4 position:SV_Position; float2 uv:TEXCOORD0; };
Vertex vs_main(uint id:SV_VertexID) {
    Vertex o; o.uv=float2((id<<1)&2,id&2);
    o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float3 pq(float3 nits) {
    const float m1=2610.0/16384.0, m2=2523.0/32.0;
    const float c1=3424.0/4096.0, c2=2413.0/128.0, c3=2392.0/128.0;
    // This shader only receives host-validated, finite nonnegative test samples.
    // FXC cannot infer the positive source patches/matrix. Keep the exact PQ
    // equation (no abs/clamp), suppress only that conservative domain warning.
#pragma warning(disable:3571)
    float3 p=pow(nits/10000.0,m1);
    return pow((c1+c2*p)/(1+c3*p),m2);
}
float4 ps_main(Vertex i):SV_Target {
    // Seven equal vertical neutral patches: 0,0.1,1,10,white,mid,peak nits.
    uint band=min((uint)(saturate(i.uv.x)*7),6);
    float n=band==0?0:band==1?0.1:band==2?1:band==3?10:band==4?ReferenceWhiteNits:band==5?(ReferenceWhiteNits+PeakNits)*0.5:PeakNits;
    float3 relative=n/ReferenceWhiteNits;
    // Bottom quarter adds BT.709 red/green/blue source patches at reference white.
    if(i.uv.y>0.75) { uint c=min((uint)(saturate(i.uv.x)*3),2); relative=c==0?float3(1,0,0):c==1?float3(0,1,0):float3(0,0,1); }
    float3 rgb2020=float3(dot(relative,float3(0.6274038959,0.3292830384,0.0433130657)),
        dot(relative,float3(0.0690972894,0.9195403951,0.0113623155)),
        dot(relative,float3(0.0163914389,0.0880133079,0.8955952532)))*ReferenceWhiteNits;
    return float4(pq(rgb2020),1);
}
