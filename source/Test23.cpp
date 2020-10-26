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

        dk::ImageLayout test_layout;
        dk::ImageLayoutMaker{device}
            .setFlags(DkImageFlags_HwCompression | DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(280, 60, 1)
            .setType(DkImageType_2D)
            .initialize(test_layout);

        auto test_allocation = pool_images->allocate(test_layout.getSize(), test_layout.getAlignment());
        auto test_block = test_allocation.getMemBlock();

        dk::Image fake_image, real_image;
        fake_image.initialize(test_layout, test_block, test_allocation.getOffset());

        dk::ImageLayoutMaker{device}
            .setFlags(DkImageFlags_HwCompression | DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(288, 48, 1)
            .setType(DkImageType_2D)
            .initialize(test_layout);
        real_image.initialize(test_layout, test_block, test_allocation.getOffset());

        std::array<dk::ImageDescriptor, 2> descriptors;
        descriptors[0].initialize(dk::ImageView{fake_image});
        descriptors[1].initialize(dk::ImageView{real_image});

        imageDescriptorSet.allocate(*pool_data);
        samplerDescriptorSet.allocate(*pool_data);

        vertexShader.load(*pool_code, "romfs:/shaders/full_tri_vsh.dksh");
        fragmentShader.load(*pool_code, "romfs:/shaders/sample_fsh.dksh");

        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
        sampler.setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);

        dk::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.initialize(sampler);

        std::array<DkImage const*, NumFramebuffers> fb_array;
        uint64_t fb_size  = layout_framebuffer.getSize();
        uint32_t fb_align = layout_framebuffer.getAlignment();
        for (unsigned i = 0; i < NumFramebuffers; i ++)
        {
            samplerDescriptorSet.update(cmdbuf, 0, samplerDescriptor);
            for (size_t i = 0; i < descriptors.size(); ++i) {
                imageDescriptorSet.update(cmdbuf, i, descriptors[i]);
            }
            imageDescriptorSet.bindForImages(cmdbuf);
            samplerDescriptorSet.bindForSamplers(cmdbuf);
            // cmdbuf.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors);

            framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
            framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

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

            dk::ImageView view{real_image};
            cmdbuf.bindRenderTargets(&view);
            cmdbuf.setScissors(0, { { 0, 0, 0xffff, 0xffff } });
            cmdbuf.clearColor(0, DkColorMask_RGBA, 1.0f, 0.0f, 0.0f, 1.0f);

            cmdbuf.setViewports(0, {{ 32, 32, 256, 256 }});
            cmdbuf.setScissors(0, { { 0, 0, FramebufferWidth, FramebufferHeight } });
            cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(1, 0));
            cmdbuf.bindRenderTargets(&colorTarget);
            cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.25f, 0.0f, 1.0f);
            cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

            // cmdbuf.barrier(DkBarrier_Full, DkInvalidateFlags_L2Cache);

            dk::ImageView view2{fake_image};
            cmdbuf.bindRenderTargets(&view2);
            cmdbuf.setScissors(0, { { 0, 0, 0xffff, 0xffff } });
            cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 1.0f, 0.0f, 1.0f);

            cmdbuf.setViewports(0, {{ 32+32+256, 32, 256, 256 }});
            cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
            cmdbuf.bindRenderTargets(&colorTarget);
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

void Test23()
{
    Test app;
    app.run();
}
