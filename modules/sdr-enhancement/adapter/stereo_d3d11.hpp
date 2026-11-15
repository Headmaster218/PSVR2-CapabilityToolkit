#pragma once
#include <d3d11.h>
#include "stereo_adapter.hpp"

namespace vr_hdr {
// Runtime-free dispatch boundary. The caller creates/owns all D3D objects.
// Use only inside the application's acquired+waited, not-yet-released frame window.
// Output is an APPLICATION-OWNED RGBA16F intermediate, never an XR swapchain image.
// The caller must ensure input/output/mask do not alias and must own the context.
// This pass changes CS state and unbinds it afterwards; it does not restore previous state.
// S_FALSE means native HDR bypass: no GPU calls, no output written; caller routes original source.
// S_OK means dispatch ENQUEUED, never that GPU completion/runtime HDR/HMD display is proven.
HRESULT DispatchIntoOwnedIntermediate(
    ID3D11DeviceContext* context,
    ID3D11ComputeShader* shader_from_verified_dxbc,
    ID3D11Buffer* constants_default_usage_48_bytes,
    ID3D11ShaderResourceView* source,
    ID3D11ShaderResourceView* optional_ui_mask,
    ID3D11UnorderedAccessView* owned_output,
    const PassRequest& request,
    Plan* inspected_plan = nullptr);
} // namespace vr_hdr
