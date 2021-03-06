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
    static constexpr unsigned StaticCmdSize = 0x4000;

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
    CDescriptorSet<16> emptyImageSet;
    CDescriptorSet<16> samplerDescriptorSet;

    DkCmdList render_cmdlist;

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
            .setFlags(DkImageFlags_HwCompression | DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_R32_Float)
            .setDimensions(1280, 720, 1)
            .setType(DkImageType_2D)
            .initialize(layout_test);

        auto test_allocation = pool_images->allocate(layout_test.getSize(), layout_test.getAlignment());
        auto test_block = test_allocation.getMemBlock();

        dk::Image test_image;
        test_image.initialize(layout_test, test_block, test_allocation.getOffset());

        dk::ImageDescriptor descriptor;
        descriptor.initialize(dk::ImageView{test_image});

        imageDescriptorSet.allocate(*pool_data);
        emptyImageSet.allocate(*pool_data);
        samplerDescriptorSet.allocate(*pool_data);

        vertexShader.load(*pool_code, "romfs:/shaders/full_tri_vsh.dksh");
        fragmentShader.load(*pool_code, "romfs:/shaders/shadow_fsh.dksh");

        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Nearest, DkFilter_Nearest);
        sampler.setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);
        sampler.setDepthCompare(true, DkCompareOp_Less);

        dk::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.initialize(sampler);

        std::array<DkImage const*, NumFramebuffers> fb_array;
        uint64_t fb_size  = layout_framebuffer.getSize();
        uint32_t fb_align = layout_framebuffer.getAlignment();
        for (unsigned i = 0; i < NumFramebuffers; i ++)
        {
            imageDescriptorSet.update(cmdbuf, 0, descriptor);
            imageDescriptorSet.bindForImages(cmdbuf);

            samplerDescriptorSet.update(cmdbuf, 0, samplerDescriptor);
            samplerDescriptorSet.bindForSamplers(cmdbuf);

            framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
            framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

            dk::ImageView rt{test_image};
            cmdbuf.bindRenderTargets(&rt);
            cmdbuf.setScissors(0, { { 0, 0, 512, 512 } });
            cmdbuf.clearColor(0, DkColorMask_RGBA, 1.0f, 0.0f, 0.0f, 1.0f);

            cmdbuf.setScissors(0, { { 0, 0, 1280, 720 } });

            dk::RasterizerState rasterizerState;
            dk::ColorState colorState;
            dk::ColorWriteState colorWriteState;
            dk::DepthStencilState depthStencilState;

            cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { vertexShader, fragmentShader });
            cmdbuf.bindRasterizerState(rasterizerState);
            cmdbuf.bindColorState(colorState);
            cmdbuf.bindColorWriteState(colorWriteState);
            cmdbuf.bindDepthStencilState(depthStencilState);

            dk::ImageView colorTarget{ framebuffers[i] };
            cmdbuf.bindRenderTargets(&colorTarget);
            cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.25f, 0.0f, 1.0f);

            cmdbuf.setViewports(0, {{ 32.0f, 32.0f, 1280.0f-64.0f, 720.0f-64.0f }});
            cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
            cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

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
        render_cmdlist = cmdbuf.finishList();
    }

    void render()
    {
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

void Test24()
{
    Test app;
    app.run();
}
