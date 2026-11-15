# SDR highlight expansion experiment

CPU reference and HLSL compute pass for two equal-rect array slices. Shader
threads are 8x8x1; Settings is 48 bytes with static offset assertions. Input is
SDR BT.709, decoded exactly once; output is application-owned RGBA16F linear
BT.709 relative white (1=referenceWhiteNits), with straight alpha preserved.

This is inverse tone-mapping/highlight expansion, not recovery of the original
HDR scene. Output values greater than one, FP16 or a successful dispatch do not
prove a runtime/display HDR path. Native HDR content bypasses expansion and
must route its original source; S_FALSE leaves the intermediate unwritten.

The D3D11 adapter accepts caller-owned shader/CB/SRV/UAV/context. It validates
settings, views and resource descriptions, writes only the intermediate, and
unbinds the compute state it changes. The caller owns device creation, UAV
capability, synchronization, color-space negotiation and final output encoding.
It must not write an XR image outside acquire+wait/release ownership.

Source and output views cover full arrays at mip 0/FirstArraySlice 0; independent
eye textures/rects need a separate gather step, not implemented here. A 48-byte
default-usage constant buffer and nonaliasing resources are required. No UI mask
is fabricated; callers must provide one or accept the limits of processing UI.

Run `node test_reference.cjs` for CPU tests. `build-offline.ps1` compiles FXC SM5,
the D3D11 adapter and rejection/bypass CPU tests with MSVC /W4 /WX. Optional
`-DxcPath` compiles SM6 separately; it is not D3D11 DXBC. Build outputs go to
ignored `.local/build/sdr-enhancement`; no GPU/runtime calls are tested.
