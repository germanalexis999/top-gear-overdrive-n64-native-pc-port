#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class F3DEX2Interpreter;
struct Vertex3D;

// The software renderer remains the reference implementation. A native GPU
// task uses private RDRAM and commits its targets before SP/DP completion;
// the independent HD interpreter never commits to the game's RDRAM.
class RasterBackend {
public:
    virtual ~RasterBackend() = default;
    virtual void triangle(F3DEX2Interpreter&, const Vertex3D&, const Vertex3D&, const Vertex3D&) = 0;
    virtual void line(F3DEX2Interpreter&, const Vertex3D&, const Vertex3D&, const Vertex3D&, float) = 0;
    virtual void rectangle(F3DEX2Interpreter&, int, int, int, int, bool, uint32_t,
                           float, float, float, float, bool) = 0;
    virtual void readback(F3DEX2Interpreter&, uint64_t, uint64_t) = 0;
    virtual void finish(F3DEX2Interpreter& r) { readback(r,0,8*1024*1024); }
};

class GPURaster final : public RasterBackend {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    GPURaster();
    ~GPURaster();
    bool available() const;
    const char* adapter_name() const;
    struct Statistics {
        uint64_t surface_allocations=0,input_allocations=0,readback_bytes=0;
        uint64_t final_readbacks=0,dispatches=0,presents=0,resize_count=0,occluded=0,present_skipped=0;
        uint64_t gpu_samples=0,gpu_elapsed_us=0,gpu_max_us=0,timing_skipped=0;
        uint64_t gpu_long_spans=0,gpu_first_long_frame=0;
    };
    Statistics statistics() const;
    void begin_frame(bool keep_on_gpu=false);
    void commit_native_results(const F3DEX2Interpreter&,uint8_t* destination);
    bool has_target(uint32_t address,uint32_t native_width) const;
    // All methods, including presentation, belong to the one GPU worker thread.
    bool present_target(void* window,uint32_t address,uint32_t native_width,
                        uint32_t visible_height,uint32_t offset,bool smooth);
    bool present_native(void* window,const std::vector<uint16_t>& pixels,
                        uint32_t width,uint32_t height,bool smooth);
    void release_presentation();
    // Test/diagnostic readback of the conversion target, never part of normal presentation.
    bool capture_output(std::vector<uint32_t>& rgba,uint32_t& width,uint32_t& height);
    void triangle(F3DEX2Interpreter&, const Vertex3D&, const Vertex3D&, const Vertex3D&) override;
    void line(F3DEX2Interpreter&, const Vertex3D&, const Vertex3D&, const Vertex3D&, float) override;
    // fill=true uses fill_color; otherwise texture rectangle, with unclipped origin.
    void rectangle(F3DEX2Interpreter&, int, int, int, int, bool, uint32_t,
                   float, float, float, float, bool) override;
    void readback(F3DEX2Interpreter&, uint64_t, uint64_t) override;
    void finish(F3DEX2Interpreter&) override;
};
