#include "stereo_d3d11.hpp"

namespace vr_hdr {
namespace {
struct ResourceRef {
    ID3D11Resource* value = nullptr;
    ~ResourceRef() { if (value) value->Release(); }
    ResourceRef() = default;
    ResourceRef(const ResourceRef&) = delete;
    ResourceRef& operator=(const ResourceRef&) = delete;
};
bool TextureDesc(ResourceRef& resource, D3D11_TEXTURE2D_DESC& desc) {
    if (!resource.value) return false;
    D3D11_RESOURCE_DIMENSION kind{};
    resource.value->GetType(&kind);
    if (kind != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    static_cast<ID3D11Texture2D*>(resource.value)->GetDesc(&desc);
    return desc.SampleDesc.Count == 1;
}
bool SameExtent(const D3D11_TEXTURE2D_DESC& d, const ImageExtent& e) {
    return d.Width == e.width && d.Height == e.height && d.ArraySize == e.array_slices;
}
DXGI_FORMAT SourceFormat(InputView view) {
    switch (view) {
    case InputView::Rgba8Srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case InputView::Bgra8Srgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case InputView::Rgba16FloatLinear: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
bool FullArrayView(const D3D11_SHADER_RESOURCE_VIEW_DESC& view, UINT slices) {
    return view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY &&
        view.Texture2DArray.MostDetailedMip == 0 &&
        view.Texture2DArray.FirstArraySlice == 0 && view.Texture2DArray.ArraySize == slices;
}
} // namespace

HRESULT DispatchIntoOwnedIntermediate(
    ID3D11DeviceContext* context, ID3D11ComputeShader* shader,
    ID3D11Buffer* constants, ID3D11ShaderResourceView* source,
    ID3D11ShaderResourceView* mask, ID3D11UnorderedAccessView* output,
    const PassRequest& request, Plan* inspected_plan) {
    const Plan plan = MakePlan(request);
    if (inspected_plan) *inspected_plan = plan;
    // These branches issue no D3D calls. Never reinterpret bypass as an output-ready texture.
    if (plan.status == Status::BypassNativeHdr) return S_FALSE;
    if (plan.status != Status::ReadyForRelativeLinearPass) return E_INVALIDARG;
    if (!context || !shader || !constants || !source || !output ||
        (request.has_ui_mask && !mask)) return E_POINTER;

    D3D11_BUFFER_DESC cb{};
    constants->GetDesc(&cb);
    if (cb.ByteWidth != sizeof(Settings) || cb.Usage != D3D11_USAGE_DEFAULT ||
        cb.BindFlags != D3D11_BIND_CONSTANT_BUFFER || cb.CPUAccessFlags != 0) return E_INVALIDARG;

    D3D11_SHADER_RESOURCE_VIEW_DESC source_view{};
    source->GetDesc(&source_view);
    if (source_view.Format != SourceFormat(request.input_view) ||
        !FullArrayView(source_view, request.source.array_slices)) return E_INVALIDARG;
    ResourceRef source_resource;
    source->GetResource(&source_resource.value);
    D3D11_TEXTURE2D_DESC source_desc{};
    if (!TextureDesc(source_resource, source_desc) || !SameExtent(source_desc, request.source)) return E_INVALIDARG;

    D3D11_UNORDERED_ACCESS_VIEW_DESC output_view{};
    output->GetDesc(&output_view);
    if (output_view.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
        output_view.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2DARRAY ||
        output_view.Texture2DArray.MipSlice != 0 || output_view.Texture2DArray.FirstArraySlice != 0 ||
        output_view.Texture2DArray.ArraySize != request.output.array_slices) return E_INVALIDARG;
    ResourceRef output_resource;
    output->GetResource(&output_resource.value);
    D3D11_TEXTURE2D_DESC output_desc{};
    if (!TextureDesc(output_resource, output_desc) || !SameExtent(output_desc, request.output) ||
        output_resource.value == source_resource.value) return E_INVALIDARG;

    ResourceRef mask_resource;
    if (request.has_ui_mask) {
        D3D11_SHADER_RESOURCE_VIEW_DESC mask_view{};
        mask->GetDesc(&mask_view);
        if ((mask_view.Format != DXGI_FORMAT_R8_UNORM && mask_view.Format != DXGI_FORMAT_R32_FLOAT) ||
            !FullArrayView(mask_view, request.source.array_slices)) return E_INVALIDARG;
        mask->GetResource(&mask_resource.value);
        D3D11_TEXTURE2D_DESC mask_desc{};
        if (!TextureDesc(mask_resource, mask_desc) || !SameExtent(mask_desc, request.source) ||
            mask_resource.value == output_resource.value || mask_resource.value == source_resource.value) return E_INVALIDARG;
    }

    // Host owns this context/pass. No device creation, XR calls, completion wait or Present here.
    context->UpdateSubresource(constants, 0, nullptr, &plan.constants, 0, 0);
    context->CSSetShader(shader, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &constants);
    ID3D11ShaderResourceView* inputs[2] = {source, request.has_ui_mask ? mask : nullptr};
    context->CSSetShaderResources(0, 2, inputs);
    context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    context->Dispatch(plan.groups_x, plan.groups_y, plan.groups_z);
    ID3D11UnorderedAccessView* no_uav = nullptr;
    ID3D11ShaderResourceView* no_srvs[2] = {nullptr, nullptr};
    ID3D11Buffer* no_cb = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    context->CSSetShaderResources(0, 2, no_srvs);
    context->CSSetConstantBuffers(0, 1, &no_cb);
    context->CSSetShader(nullptr, nullptr, 0);
    return S_OK;
}
} // namespace vr_hdr
