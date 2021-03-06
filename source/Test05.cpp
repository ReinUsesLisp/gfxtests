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
    static constexpr unsigned StaticCmdSize = 0x40000;

    dk::UniqueDevice device;
    dk::UniqueQueue queue;

    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;

    CShader vertexShader;
    CShader fragmentShader;
    CShader redShader;
    CShader blueShader;

    dk::UniqueCmdBuf cmdbuf;

    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;

    static constexpr uint32_t DIM = 64;

    CDescriptorSet<DIM> imageDescriptorSet;
    CDescriptorSet<DIM> emptyImageSet;
    CDescriptorSet<DIM> samplerDescriptorSet;

    DkCmdList render_cmdlist;

public:
    Test()
    {
        device = dk::DeviceMaker{}.create();

        queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();

        pool_images.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 64*1024*1024);
        pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 64*1024*1024);
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
            .setFlags(DkImageFlags_UsageRender)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(DIM, DIM, DIM)
            .setMipLevels(2)
            .setType(DkImageType_3D)
            .initialize(layout_test);

        auto test_allocation = pool_images->allocate(layout_test.getSize(), layout_test.getAlignment());
        auto test_block = test_allocation.getMemBlock();
        memset(test_block.getCpuAddr(), 0xff, test_block.getSize());

        dk::Image test_image;
        test_image.initialize(layout_test, test_block, test_allocation.getOffset());

        std::array<dk::ImageDescriptor, 1> descriptors;
        for (size_t i = 0; i < descriptors.size(); ++i) {
            descriptors[i].initialize(dk::ImageView{test_image}.setMipLevels(1, 1));
        }

        imageDescriptorSet.allocate(*pool_data);
        emptyImageSet.allocate(*pool_data);
        samplerDescriptorSet.allocate(*pool_data);

        vertexShader.load(*pool_code, "romfs:/shaders/full_tri_vsh.dksh");
        fragmentShader.load(*pool_code, "romfs:/shaders/sample_3d_fsh.dksh");
        redShader.load(*pool_code, "romfs:/shaders/red_fsh.dksh");
        blueShader.load(*pool_code, "romfs:/shaders/blue_fsh.dksh");

        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
        sampler.setWrapMode(DkWrapMode_Clamp, DkWrapMode_Clamp, DkWrapMode_Clamp);
        sampler.setBorderColor(0.5f, 0.5f, 0.5f, 1.0f);

        dk::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.initialize(sampler);

        std::array<DkImage const*, NumFramebuffers> fb_array;
        uint64_t fb_size  = layout_framebuffer.getSize();
        uint32_t fb_align = layout_framebuffer.getAlignment();
        for (unsigned i = 0; i < NumFramebuffers; i ++)
        {
            samplerDescriptorSet.update(cmdbuf, 0, samplerDescriptor);
            for (size_t j = 0; j < descriptors.size(); ++j) {
                imageDescriptorSet.update(cmdbuf, j, descriptors[j]);
            }
            samplerDescriptorSet.bindForSamplers(cmdbuf);

            framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
            framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

            emptyImageSet.bindForImages(cmdbuf);

            cmdbuf.setScissors(0, { { 0, 0, 4096, 4096 } });
            cmdbuf.setViewports(0, {{ 0, 0, DIM/2, DIM/2 }});

            for (uint32_t j = 0; j < DIM/2; ++j) {
                auto& fs = j % 2 == 0 ? redShader : blueShader;
                cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { vertexShader, fs });

                dk::ImageView view{test_image};
                view.setLayers(j, 1);
                view.setMipLevels(1, 1);

                cmdbuf.bindRenderTargets(&view);
                cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);
            }

            imageDescriptorSet.bindForImages(cmdbuf);

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

            cmdbuf.setViewports(0, {{ 32, 256, 1280-64, 256 }});
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

void Test05()
{
    Test app;
    app.run();
}
