#include "SampleFramework/CDescriptorSet.h"
#include "SampleFramework/CShader.h"
#include "SampleFramework/CApplication.h"
#include "SampleFramework/CMemPool.h"

#include <array>
#include <optional>

namespace {

class Test final : public CApplication
{
    static constexpr unsigned NumFramebuffers = 2;
    static constexpr uint32_t FramebufferWidth = 1280;
    static constexpr uint32_t FramebufferHeight = 720;
    static constexpr unsigned StaticCmdSize = 0x1000;

    dk::UniqueDevice device;
    dk::UniqueQueue queue;

    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;

    CShader vertexShader;
    CShader fragmentShader;

    dk::UniqueCmdBuf cmdbuf;

    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;

    CDescriptorSet<16> imageDescriptorSet;
    CDescriptorSet<16> samplerDescriptorSet;

    DkCmdList render_cmdlist;

    struct Pixel {
        u8 r, g, b, a;
    };
    struct Image {
        u8* data;
        int pitch;

        void write(int x, int y, Pixel color)
        {
            memcpy(data + y * pitch + x * sizeof(Pixel), &color, sizeof(color));
        }
    };
    Image image;

public:
    Test()
    {
        device = dk::DeviceMaker{}.create();

        queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();

        pool_images.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
        pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);
        pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);

        cmdbuf = dk::CmdBufMaker{device}.create();
        CMemPool::Handle cmdmem = pool_data->allocate(StaticCmdSize);
        cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

        createFramebufferResources();
    }

    ~Test()
    {
        // Destroy the framebuffer resources
        destroyFramebufferResources();
    }

    void createFramebufferResources()
    {
        dk::ImageLayout layout_framebuffer;
        dk::ImageLayoutMaker{device}
            .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(FramebufferWidth, FramebufferHeight)
            .initialize(layout_framebuffer);

        dk::ImageLayout layout_test;
        dk::ImageLayoutMaker{device}
            .setFlags(DkImageFlags_PitchLinear)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(512, 512, 1)
            .setType(DkImageType_2D)
            .setPitchStride(512*4)
            .initialize(layout_test);

        auto test_allocation = pool_images->allocate(layout_test.getSize(), layout_test.getAlignment());
        auto test_block = test_allocation.getMemBlock();

        image.data = static_cast<u8*>(test_block.getCpuAddr());
        image.pitch = 512*4;
        for (int y = 0; y < 512; ++y) {
            for (int x = 0; x < 512; ++x) {
                image.write(x, y, {96,96,96,255});
            }
        }

        dk::Image test_image;
        test_image.initialize(layout_test, test_block, test_allocation.getOffset());

        dk::ImageDescriptor descriptor;
        descriptor.initialize(dk::ImageView{test_image});

        imageDescriptorSet.allocate(*pool_data);
        samplerDescriptorSet.allocate(*pool_data);

        vertexShader.load(*pool_code, "romfs:/shaders/full_tri_vsh.dksh");
        fragmentShader.load(*pool_code, "romfs:/shaders/sample_fsh.dksh");

        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
        sampler.setWrapMode(DkWrapMode_Clamp, DkWrapMode_Clamp, DkWrapMode_Clamp);
        sampler.setBorderColor(0.0f, 0.125f, 0.0f, 1.0f);

        dk::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.initialize(sampler);

        std::array<DkImage const*, NumFramebuffers> fb_array;
        uint64_t fb_size  = layout_framebuffer.getSize();
        uint32_t fb_align = layout_framebuffer.getAlignment();
        for (unsigned i = 0; i < NumFramebuffers; i ++)
        {
            imageDescriptorSet.update(cmdbuf, 0, descriptor);
            samplerDescriptorSet.update(cmdbuf, 0, samplerDescriptor);

            imageDescriptorSet.bindForImages(cmdbuf);
            samplerDescriptorSet.bindForSamplers(cmdbuf);

            framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
            framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

            dk::ImageView colorTarget{ framebuffers[i] };

            cmdbuf.bindRenderTargets(&colorTarget);
            framebuffer_cmdlists[i] = cmdbuf.finishList();

            fb_array[i] = &framebuffers[i];
        }

        swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), fb_array}.create();

        recordStaticCommands();
    }

    void destroyFramebufferResources()
    {
        if (!swapchain) return;

        queue.waitIdle();
        cmdbuf.clear();
        swapchain.destroy();

        for (unsigned i = 0; i < NumFramebuffers; i ++)
            framebuffers_mem[i].destroy();
    }

    void recordStaticCommands()
    {
        dk::RasterizerState rasterizerState;
        dk::ColorState colorState;
        dk::ColorWriteState colorWriteState;
        dk::DepthStencilState depthStencilState;

        cmdbuf.setScissors(0, { { 0, 0, FramebufferWidth, FramebufferHeight } });
        cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.125f, 0.0f, 1.0f);
        cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { vertexShader, fragmentShader });
        cmdbuf.bindRasterizerState(rasterizerState);
        cmdbuf.bindColorState(colorState);
        cmdbuf.bindColorWriteState(colorWriteState);
        cmdbuf.bindDepthStencilState(depthStencilState);

        cmdbuf.setViewports(0, {{ 32, 32, 512, 512 }});
        cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
        cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

        render_cmdlist = cmdbuf.finishList();
    }

    std::pair<int, int> pos(unsigned idx) {
        static constexpr std::pair<int, int> lut[] = {
            {0,0}, {1,0}, {2,0}, {3,0}, {4,0}, {5,0}, {6,0}, {7,0},
            {7,1}, {7,2}, {7,3}, {7,4}, {7,5}, {7,6}, {7,7},
            {6,7}, {5,7}, {4,7}, {3,7}, {2,7}, {1,7}, {0,7},
            {0,6}, {0,5}, {0,4}, {0,3}, {0,2}, {0,1},
        };
        return lut[idx % std::size(lut)];
    }

    static constexpr Pixel colors[]{
        {96,96,96,255},
        {123,96,96,255},
        {150,96,96,255},
        {176,96,96,255},
        {203,96,96,255},
        {229,96,96,255},
        {255,96,96,255},
    };

    void writeSquare(unsigned idx, Pixel color) {
        auto [block_x, block_y] = pos(idx);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                image.write(block_x*64 + x, block_y*64 + y, color);
            }
        }
    }

    unsigned squareIdx = 0;
    void animate() {
        for (unsigned i = 0; i < std::size(colors); ++i) {
            writeSquare(squareIdx / 4 + i, colors[i]);
        }
        ++squareIdx;
    }

    void render()
    {
        queue.waitIdle();
        animate();

        int slot = queue.acquireImage(swapchain);
        queue.submitCommands(framebuffer_cmdlists[slot]);
        queue.submitCommands(render_cmdlist);
        queue.presentImage(swapchain, slot);
    }

    bool onFrame(u64 ns) override
    {
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS) {
            return false;
        }
        render();
        return true;
    }
};

} // Anonymous namespace

void Test02()
{
    Test app;
    app.run();
}
