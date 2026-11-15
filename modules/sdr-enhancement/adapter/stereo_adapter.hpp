#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// CPU-only contract. No Windows/OpenXR headers, loaders, devices, or HDR-ready flag.
namespace vr_hdr {
struct alignas(16) Settings {
    std::uint32_t extent[2];
    std::uint32_t eye_count;
    std::uint32_t has_ui_mask;
    std::uint32_t source_offset[2];
    std::uint32_t source_first_layer;
    std::uint32_t output_first_layer;
    float reference_white_nits;
    float peak_nits;
    float knee;
    float strength;
};
static_assert(std::is_standard_layout<Settings>::value, "Settings must be standard layout");
static_assert(std::is_trivially_copyable<Settings>::value, "Settings must be byte-copyable");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE binary32 required");
static_assert(sizeof(Settings) == 48 && alignof(Settings) == 16, "HLSL Settings ABI");
static_assert(offsetof(Settings, extent) == 0, "extent offset");
static_assert(offsetof(Settings, eye_count) == 8, "eye count offset");
static_assert(offsetof(Settings, has_ui_mask) == 12, "UI mask offset");
static_assert(offsetof(Settings, source_offset) == 16, "source offset");
static_assert(offsetof(Settings, source_first_layer) == 24, "source layer offset");
static_assert(offsetof(Settings, output_first_layer) == 28, "output layer offset");
static_assert(offsetof(Settings, reference_white_nits) == 32, "white offset");
static_assert(offsetof(Settings, peak_nits) == 36, "peak offset");
static_assert(offsetof(Settings, knee) == 40, "knee offset");
static_assert(offsetof(Settings, strength) == 44, "strength offset");

enum class Content { SdrBt709, NativeHdrLinearBt709, NativeHdrPqBt2020, Unknown };
enum class InputView { Rgba8Srgb, Bgra8Srgb, Rgba16FloatLinear, Unsupported };
enum class Decode { HardwareSrgbOnce, AlreadyLinear, Unknown };
enum class Alpha { Straight, Premultiplied, Unknown };
enum class OutputView { Rgba16Float, Unsupported };
enum class Status { ReadyForRelativeLinearPass, BypassNativeHdr, Unsupported };
enum class OutputMeaning { LinearBt709RelativeWhite, PreserveSourceUnmodified, None };
struct ImageExtent { std::uint32_t width, height, array_slices; };
struct Rect { std::uint32_t x, y, width, height; };
struct Controls {
    float reference_white_nits = 200.0f; // illustrative, never a panel measurement
    float peak_nits = 800.0f;
    float knee = 0.75f;
    float strength = 0.5f;
};
struct PassRequest {
    Content content = Content::Unknown;
    InputView input_view = InputView::Unsupported;
    Decode decode = Decode::Unknown;
    Alpha alpha = Alpha::Unknown;
    OutputView output_view = OutputView::Unsupported;
    ImageExtent source{};
    ImageExtent output{};
    Rect source_rect{}; // common rect in both array slices; no different-eye rect support
    std::uint32_t source_first_layer = 0; // relative to an SRV with FirstArraySlice=0
    std::uint32_t output_first_layer = 0; // relative to a UAV with FirstArraySlice=0
    std::uint32_t eye_count = 2;
    bool has_ui_mask = false; // a processing input, never a runtime capability assertion
    Controls controls{};
};
struct Plan {
    Status status = Status::Unsupported;
    OutputMeaning meaning = OutputMeaning::None;
    const char* reason = "not planned";
    Settings constants{};
    std::uint32_t groups_x = 0, groups_y = 0, groups_z = 0;
};
inline bool FitsRect(const Rect& r, const ImageExtent& im) {
    return r.width != 0 && r.height != 0 &&
        std::uint64_t(r.x) + r.width <= im.width &&
        std::uint64_t(r.y) + r.height <= im.height;
}
inline bool FitsPair(std::uint32_t first, std::uint32_t count) {
    return std::uint64_t(first) + 2 <= count;
}
inline bool ValidControls(const Controls& c) {
    return std::isfinite(c.reference_white_nits) && std::isfinite(c.peak_nits) &&
        std::isfinite(c.knee) && std::isfinite(c.strength) &&
        c.reference_white_nits >= std::numeric_limits<float>::min() && c.peak_nits >= c.reference_white_nits &&
        c.knee >= 0 && c.knee < 1 && c.strength >= 0 && c.strength <= 1 &&
        std::isfinite(c.peak_nits / c.reference_white_nits) &&
        c.peak_nits / c.reference_white_nits <= 65504.0f; // RGBA16F finite maximum
}
inline Plan MakePlan(const PassRequest& r) {
    Plan p{};
    if (r.content == Content::NativeHdrLinearBt709 || r.content == Content::NativeHdrPqBt2020) {
        p.status = Status::BypassNativeHdr;
        p.meaning = OutputMeaning::PreserveSourceUnmodified;
        p.reason = "native HDR: no inverse tone mapping, no clamp, no output texture written";
        return p;
    }
    if (r.content != Content::SdrBt709) { p.reason = "unsupported or unknown content semantics"; return p; }
    if (!ValidControls(r.controls)) { p.reason = "unsupported controls: invalid finite range"; return p; }
    const bool srgb = r.input_view == InputView::Rgba8Srgb || r.input_view == InputView::Bgra8Srgb;
    const bool linear = r.input_view == InputView::Rgba16FloatLinear;
    if (!((srgb && r.decode == Decode::HardwareSrgbOnce) || (linear && r.decode == Decode::AlreadyLinear))) {
        p.reason = "unsupported input/decode pair: decode exactly once"; return p;
    }
    if (r.alpha != Alpha::Straight) { p.reason = "unsupported alpha: straight alpha required"; return p; }
    if (r.output_view != OutputView::Rgba16Float) { p.reason = "unsupported output: RGBA16F required"; return p; }
    if (r.eye_count != 2 || !FitsRect(r.source_rect, r.source) ||
        !FitsPair(r.source_first_layer, r.source.array_slices) ||
        !FitsPair(r.output_first_layer, r.output.array_slices) ||
        r.output.width < r.source_rect.width || r.output.height < r.source_rect.height) {
        p.reason = "unsupported stereo layout: two slices, valid common rect and output extent required"; return p;
    }
    // D3D11 Texture2D maximum dimension; prevents group arithmetic overflow too.
    if (r.source.width > 16384 || r.source.height > 16384 ||
        r.output.width > 16384 || r.output.height > 16384 ||
        r.source.array_slices > 2048 || r.output.array_slices > 2048) {
        p.reason = "unsupported D3D11 texture extent"; return p;
    }
    p.constants = {{r.source_rect.width, r.source_rect.height}, 2, r.has_ui_mask ? 1u : 0u,
        {r.source_rect.x, r.source_rect.y}, r.source_first_layer, r.output_first_layer,
        r.controls.reference_white_nits, r.controls.peak_nits, r.controls.knee, r.controls.strength};
    p.groups_x = (r.source_rect.width + 7) / 8;
    p.groups_y = (r.source_rect.height + 7) / 8;
    p.groups_z = 2;
    p.status = Status::ReadyForRelativeLinearPass;
    p.meaning = OutputMeaning::LinearBt709RelativeWhite;
    p.reason = "CPU contract only: relative linear intermediate, no runtime or HMD assertion";
    return p;
}
// Resolve this filename against the caller's own build/output directory.
inline constexpr const wchar_t* kDxbcShaderFileName = L"stereo_expand.cs_5_0.cso";
} // namespace vr_hdr
