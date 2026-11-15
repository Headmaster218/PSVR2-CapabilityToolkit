#include "stereo_adapter.hpp"
#include "stereo_d3d11.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <stdexcept>

using namespace vr_hdr;
namespace {
unsigned tests = 0;
void Check(bool v, const char* name) { if (!v) throw std::runtime_error(name); ++tests; }
PassRequest Good() {
    PassRequest r{};
    r.content=Content::SdrBt709; r.input_view=InputView::Rgba8Srgb;
    r.decode=Decode::HardwareSrgbOnce; r.alpha=Alpha::Straight;
    r.output_view=OutputView::Rgba16Float;
    r.source={2048,2048,4}; r.output={1024,768,4};
    r.source_rect={20,30,1024,768}; r.source_first_layer=1; r.output_first_layer=2;
    return r;
}
bool Unsupported(const PassRequest& r) { return MakePlan(r).status == Status::Unsupported; }
} // namespace
int main() {
    try {
        auto r=Good(); const auto p=MakePlan(r);
        Check(p.status==Status::ReadyForRelativeLinearPass && p.meaning==OutputMeaning::LinearBt709RelativeWhite, "SDR route");
        Check(p.groups_x==128 && p.groups_y==96 && p.groups_z==2, "stereo dispatch grid");
        Check(p.constants.source_offset[0]==20 && p.constants.source_first_layer==1 && p.constants.output_first_layer==2, "rect and slices");
        unsigned char bytes[48]{}; std::memcpy(bytes,&p.constants,sizeof(bytes));
        std::uint32_t first_layer=0; float white=0;
        std::memcpy(&first_layer,bytes+24,sizeof(first_layer)); std::memcpy(&white,bytes+32,sizeof(white));
        Check(first_layer==1 && white==200.0f, "packed constant offsets");
        r=Good(); r.source_rect.width=1023;
        Check(MakePlan(r).groups_x==128, "ceil dispatch");
        r=Good(); r.input_view=InputView::Rgba16FloatLinear; r.decode=Decode::AlreadyLinear;
        Check(!Unsupported(r), "already-linear float avoids double decode");
        r.decode=Decode::HardwareSrgbOnce; Check(Unsupported(r), "double decode rejected");
        r=Good(); r.decode=Decode::AlreadyLinear; Check(Unsupported(r), "encoded sRGB without decode rejected");
        r=Good(); r.alpha=Alpha::Premultiplied; Check(Unsupported(r), "premultiplied alpha rejected");
        r=Good(); r.output_view=OutputView::Unsupported; Check(Unsupported(r), "unsupported output rejected");
        r=Good(); r.eye_count=1; Check(Unsupported(r), "non-stereo rejected");
        r=Good(); r.source_rect.width=0; Check(Unsupported(r), "zero region rejected");
        r=Good(); r.source_rect.x=0xffffffffu; Check(Unsupported(r), "rect integer overflow rejected");
        r=Good(); r.source_first_layer=0xffffffffu; Check(Unsupported(r), "slice integer overflow rejected");
        r=Good(); r.output_first_layer=3; Check(Unsupported(r), "output pair bounds");
        r=Good(); r.output.width=100; Check(Unsupported(r), "output extent bounds");
        r=Good(); r.source.width=16385; Check(Unsupported(r), "D3D11 dimension limit");
        r=Good(); r.controls.reference_white_nits=0; Check(Unsupported(r), "zero reference white");
        r=Good(); r.controls.peak_nits=100; Check(Unsupported(r), "peak below white");
        r=Good(); r.controls.knee=1; Check(Unsupported(r), "knee singularity");
        r=Good(); r.controls.strength=-0.1f; Check(Unsupported(r), "negative strength");
        r=Good(); r.controls.peak_nits=std::numeric_limits<float>::infinity(); Check(Unsupported(r), "nonfinite controls");
        r=Good(); r.controls.reference_white_nits=std::numeric_limits<float>::denorm_min();
        r.controls.peak_nits=std::numeric_limits<float>::max(); Check(Unsupported(r), "float32 ratio overflow");
        r=Good(); r.controls.reference_white_nits=1.0e-40f; r.controls.peak_nits=1.0e-40f;
        Check(Unsupported(r), "subnormal reference white rejected before GPU denominator");
        r=Good(); r.controls.reference_white_nits=1; r.controls.peak_nits=65504;
        Check(!Unsupported(r), "RGBA16F maximum accepted");
        r.controls.peak_nits=65505; Check(Unsupported(r), "finite ratio exceeding half range rejected");
        r=Good(); r.has_ui_mask=true; Check(MakePlan(r).constants.has_ui_mask==1, "explicit per-pixel UI mask");
        r=Good(); r.content=Content::Unknown; Check(Unsupported(r), "unknown content unsupported");
        for (auto kind : {Content::NativeHdrLinearBt709,Content::NativeHdrPqBt2020}) {
            r=Good(); r.content=kind;
            r.controls.peak_nits=std::numeric_limits<float>::quiet_NaN(); // irrelevant to native bypass
            const auto native=MakePlan(r);
            Check(native.status==Status::BypassNativeHdr && native.meaning==OutputMeaning::PreserveSourceUnmodified && native.groups_z==0,
                "native HDR bypass has no output dispatch");
            Check(DispatchIntoOwnedIntermediate(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,r)==S_FALSE,
                "native bypass executes with null bindings and no device calls");
        }
        r=Good(); r.content=Content::Unknown;
        Check(DispatchIntoOwnedIntermediate(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,r)==E_INVALIDARG,
            "unsupported executes with null bindings and no device calls");
        r=Good();
        Check(DispatchIntoOwnedIntermediate(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,r)==E_POINTER,
            "missing bindings rejected before device calls");
        std::printf("CPU contract assertions passed: %u\nNo device created; only pre-D3D rejection/bypass branches invoked.\n",tests);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
    }
}
