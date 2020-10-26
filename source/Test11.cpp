#include "SampleFramework/CDescriptorSet.h"
#include "SampleFramework/CShader.h"
#include "SampleFramework/CApplication.h"
#include "SampleFramework/CMemPool.h"

#include <array>
#include <optional>

namespace {

constexpr u32 GOB_SIZE_X = 64;
constexpr u32 GOB_SIZE_Y = 8;
constexpr u32 GOB_SIZE_Z = 1;
constexpr u32 GOB_SIZE = GOB_SIZE_X * GOB_SIZE_Y * GOB_SIZE_Z;

constexpr std::size_t GOB_SIZE_X_SHIFT = 6;
constexpr std::size_t GOB_SIZE_Y_SHIFT = 3;
constexpr std::size_t GOB_SIZE_Z_SHIFT = 0;
constexpr std::size_t GOB_SIZE_SHIFT = GOB_SIZE_X_SHIFT + GOB_SIZE_Y_SHIFT + GOB_SIZE_Z_SHIFT;

template <std::size_t N, std::size_t M, u32 Align>
struct alignas(64) SwizzleTable {
    static_assert(M * Align == 64, "Swizzle Table does not align to GOB");
    constexpr SwizzleTable() {
        for (u32 y = 0; y < N; ++y) {
            for (u32 x = 0; x < M; ++x) {
                const u32 x2 = x * Align;
                values[y][x] = static_cast<u16>(((x2 % 64) / 32) * 256 + ((y % 8) / 2) * 64 +
                                                ((x2 % 32) / 16) * 32 + (y % 2) * 16 + (x2 % 16));
            }
        }
    }
    const std::array<u16, M>& operator[](std::size_t index) const {
        return values[index];
    }
    std::array<std::array<u16, M>, N> values{};
};
constexpr auto LEGACY_SWIZZLE_TABLE = SwizzleTable<GOB_SIZE_X, GOB_SIZE_X, GOB_SIZE_Z>();

size_t Swizzle(u32 width, u32 bytes_per_pixel, u32 block_height, u32 origin_x, u32 origin_y) {
    const u32 stride = width * bytes_per_pixel;
    const u32 gobs_in_x = (stride + GOB_SIZE_X - 1) / GOB_SIZE_X;
    const u32 block_size = gobs_in_x << (GOB_SIZE_SHIFT + block_height);

    const u32 block_height_mask = (1U << block_height) - 1;
    const u32 x_shift = static_cast<u32>(GOB_SIZE_SHIFT) + block_height;

    const u32 dst_y = origin_y;
    const auto& table = LEGACY_SWIZZLE_TABLE[dst_y % GOB_SIZE_Y];

    const u32 block_y = dst_y >> GOB_SIZE_Y_SHIFT;
    const u32 dst_offset_y =
         (block_y >> block_height) * block_size +
        ((block_y & block_height_mask) << GOB_SIZE_SHIFT);
        
    const u32 dst_x = origin_x * bytes_per_pixel;
    const u32 offset_x = (dst_x >> GOB_SIZE_X_SHIFT) << x_shift;

    return dst_offset_y + offset_x + table[dst_x % GOB_SIZE_X];
}

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
        u32 width;
        u32 block_height;

        void write(int x, int y, Pixel color)
        {
            size_t o = Swizzle(width, 4, block_height, x, y);
            memcpy(data + o, &color, sizeof(color));
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
            .setFlags(DkImageFlags_CustomTileSize)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(2048, 512, 1)
            .setMipLevels(12)
            .setType(DkImageType_2D)
            .setTileSize(DkTileSize_SixteenGobs)
            .initialize(layout_test);

        auto test_allocation = pool_images->allocate(layout_test.getSize(), layout_test.getAlignment());
        auto test_block = test_allocation.getMemBlock();

        image.data = static_cast<u8*>(test_block.getCpuAddr());
        image.width = 2048;
        image.block_height = 4;
        for (u32 y = 0; y < 512; ++y) {
            for (u32 x = 0; x < 2048; ++x) {
                image.write(x, y, {x/8,y/2,0,255});
            }
        }

        dk::Image test_image;
        test_image.initialize(layout_test, test_block, test_allocation.getOffset());

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
            dk::ImageDescriptor descriptor;
            descriptor.initialize(dk::ImageView{test_image}.setFormat(i == 1 ? DkImageFormat_RGBA8_Unorm : DkImageFormat_RGBA8_Unorm_sRGB));

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

void Test11()
{
    Test app;
    app.run();
}
