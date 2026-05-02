// Copyright (c) Wojciech Figat. All rights reserved.

#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"
#include "Engine/Graphics/RenderGraph/RenderGraphBuilder.h"
#include "Engine/Graphics/RenderGraph/RenderGraphCompiler.h"
#include "Engine/Graphics/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Graphics/RenderGraph/RenderGraphTypes.h"
#include "Engine/Graphics/GPUContext.h"
#include "Engine/Graphics/GPUDevice.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Collections/Array.h"

#if !BUILD_RELEASE

namespace
{
    // Mock RenderGraph implementation for testing
    class MockRenderGraph : public RenderGraphBuilder
    {
    private:
        struct TextureResource
        {
            String Name;
            RenderGraphTextureDesc Desc;
            GPUTexture* ExternalTexture;
            int32 RefCount;
        };

        struct BufferResource
        {
            String Name;
            RenderGraphBufferDesc Desc;
            GPUBuffer* ExternalBuffer;
            int32 RefCount;
        };

        Array<TextureResource> _textures;
        Array<BufferResource> _buffers;
        Array<RenderGraphPass*> _passes;

    public:
        MockRenderGraph()
        {
        }

        ~MockRenderGraph()
        {
            for (auto* pass : _passes)
                Delete(pass);
        }

        RenderGraphTextureRef CreateTexture(const RenderGraphTextureDesc& desc) override
        {
            TextureResource res;
            res.Name = desc.Name;
            res.Desc = desc;
            res.ExternalTexture = nullptr;
            res.RefCount = 0;
            _textures.Add(res);
            return RenderGraphTextureRef(_textures.Count() - 1);
        }

        RenderGraphTextureRef ImportTexture(const String& name, GPUTexture* texture) override
        {
            TextureResource res;
            res.Name = name;
            res.ExternalTexture = texture;
            res.RefCount = 0;
            _textures.Add(res);
            return RenderGraphTextureRef(_textures.Count() - 1);
        }

        RenderGraphBufferRef CreateBuffer(const RenderGraphBufferDesc& desc) override
        {
            BufferResource res;
            res.Name = desc.Name;
            res.Desc = desc;
            res.ExternalBuffer = nullptr;
            res.RefCount = 0;
            _buffers.Add(res);
            return RenderGraphBufferRef(_buffers.Count() - 1);
        }

        RenderGraphBufferRef ImportBuffer(const String& name, GPUBuffer* buffer) override
        {
            BufferResource res;
            res.Name = name;
            res.ExternalBuffer = buffer;
            res.RefCount = 0;
            _buffers.Add(res);
            return RenderGraphBufferRef(_buffers.Count() - 1);
        }

        GPUTexture* GetTexture(RenderGraphTextureRef handle) override
        {
            if (handle.IsValid() && handle.Index < _textures.Count())
                return _textures[handle.Index].ExternalTexture;
            return nullptr;
        }

        GPUBuffer* GetBuffer(RenderGraphBufferRef handle) override
        {
            if (handle.IsValid() && handle.Index < _buffers.Count())
                return _buffers[handle.Index].ExternalBuffer;
            return nullptr;
        }

        void AddPass(RenderGraphPass* pass)
        {
            _passes.Add(pass);
            pass->Setup(*this);
        }

        int32 GetTextureCount() const { return _textures.Count(); }
        int32 GetBufferCount() const { return _buffers.Count(); }
        int32 GetPassCount() const { return _passes.Count(); }

        const Array<RenderGraphPass*>& GetPasses() const { return _passes; }
    };

    // Test pass implementations
    class TestRasterPass : public RenderGraphRasterPass
    {
    private:
        RenderGraphTextureRef _output;

    public:
        TestRasterPass(const String& name)
            : RenderGraphRasterPass(name)
        {
        }

        void Setup(RenderGraphBuilder& builder) override
        {
            RenderGraphTextureDesc desc;
            desc.Name = TEXT("TestOutput");
            desc.Width = 1920;
            desc.Height = 1080;
            desc.Format = PixelFormat::R8G8B8A8_UNorm;
            desc.Flags = GPUTextureFlags::RenderTarget | GPUTextureFlags::ShaderResource;

            _output = builder.CreateTexture(desc);
            WriteTexture(_output);
        }

        void Execute(GPUContext* context) override
        {
            // Mock execution
        }
    };

    class TestComputePass : public RenderGraphComputePass
    {
    private:
        RenderGraphTextureRef _input;
        RenderGraphTextureRef _output;

    public:
        TestComputePass(const String& name)
            : RenderGraphComputePass(name)
        {
        }

        void Setup(RenderGraphBuilder& builder) override
        {
            RenderGraphTextureDesc desc;
            desc.Name = TEXT("ComputeInput");
            desc.Width = 1920;
            desc.Height = 1080;
            desc.Format = PixelFormat::R8G8B8A8_UNorm;
            desc.Flags = GPUTextureFlags::ShaderResource | GPUTextureFlags::UnorderedAccess;

            _input = builder.CreateTexture(desc);
            ReadTexture(_input);

            desc.Name = TEXT("ComputeOutput");
            _output = builder.CreateTexture(desc);
            WriteTexture(_output);
        }

        void Execute(GPUContext* context) override
        {
            // Mock execution
        }
    };

    class TestCopyPass : public RenderGraphCopyPass
    {
    private:
        RenderGraphTextureRef _source;
        RenderGraphTextureRef _dest;

    public:
        TestCopyPass(const String& name)
            : RenderGraphCopyPass(name)
        {
        }

        void Setup(RenderGraphBuilder& builder) override
        {
            RenderGraphTextureDesc desc;
            desc.Name = TEXT("CopySource");
            desc.Width = 1920;
            desc.Height = 1080;
            desc.Format = PixelFormat::R8G8B8A8_UNorm;
            desc.Flags = GPUTextureFlags::ShaderResource;

            _source = builder.CreateTexture(desc);
            ReadTexture(_source);

            desc.Name = TEXT("CopyDest");
            _dest = builder.CreateTexture(desc);
            WriteTexture(_dest);
        }

        void Execute(GPUContext* context) override
        {
            // Mock execution
        }
    };
}

// Test: Basic pass creation and setup
bool TestRenderGraph_BasicPassCreation()
{
    LOG(Info, "TestRenderGraph: Basic pass creation");

    MockRenderGraph graph;
    auto* pass = New<TestRasterPass>(TEXT("TestPass"));
    graph.AddPass(pass);

    if (graph.GetPassCount() != 1)
    {
        LOG(Error, "Expected 1 pass, got {0}", graph.GetPassCount());
        return false;
    }

    if (graph.GetTextureCount() == 0)
    {
        LOG(Error, "Expected at least 1 texture resource");
        return false;
    }

    LOG(Info, "TestRenderGraph: Basic pass creation - PASSED");
    return true;
}

// Test: Multiple passes with dependencies
bool TestRenderGraph_MultiplePasses()
{
    LOG(Info, "TestRenderGraph: Multiple passes with dependencies");

    MockRenderGraph graph;
    
    auto* pass1 = New<TestRasterPass>(TEXT("Pass1"));
    auto* pass2 = New<TestComputePass>(TEXT("Pass2"));
    auto* pass3 = New<TestCopyPass>(TEXT("Pass3"));

    graph.AddPass(pass1);
    graph.AddPass(pass2);
    graph.AddPass(pass3);

    if (graph.GetPassCount() != 3)
    {
        LOG(Error, "Expected 3 passes, got {0}", graph.GetPassCount());
        return false;
    }

    LOG(Info, "TestRenderGraph: Multiple passes - PASSED");
    return true;
}

// Test: Resource creation and management
bool TestRenderGraph_ResourceManagement()
{
    LOG(Info, "TestRenderGraph: Resource management");

    MockRenderGraph graph;

    // Create texture
    RenderGraphTextureDesc texDesc;
    texDesc.Name = TEXT("TestTexture");
    texDesc.Width = 1920;
    texDesc.Height = 1080;
    texDesc.Format = PixelFormat::R8G8B8A8_UNorm;
    texDesc.Flags = GPUTextureFlags::RenderTarget;

    auto texRef = graph.CreateTexture(texDesc);
    if (!texRef.IsValid())
    {
        LOG(Error, "Failed to create texture");
        return false;
    }

    // Create buffer
    RenderGraphBufferDesc bufDesc;
    bufDesc.Name = TEXT("TestBuffer");
    bufDesc.Size = 1024;
    bufDesc.Stride = 16;
    bufDesc.Flags = GPUBufferFlags::ShaderResource;

    auto bufRef = graph.CreateBuffer(bufDesc);
    if (!bufRef.IsValid())
    {
        LOG(Error, "Failed to create buffer");
        return false;
    }

    if (graph.GetTextureCount() == 0 || graph.GetBufferCount() == 0)
    {
        LOG(Error, "Resources not properly tracked");
        return false;
    }

    LOG(Info, "TestRenderGraph: Resource management - PASSED");
    return true;
}

// Test: Pass culling
bool TestRenderGraph_PassCulling()
{
    LOG(Info, "TestRenderGraph: Pass culling");

    MockRenderGraph graph;

    // Create a pass that writes to a resource that's never read
    auto* pass1 = New<TestRasterPass>(TEXT("UnusedPass"));
    graph.AddPass(pass1);

    // The pass should be marked as cullable
    if (!pass1->CanCull())
    {
        LOG(Error, "Pass should be cullable");
        return false;
    }

    LOG(Info, "TestRenderGraph: Pass culling - PASSED");
    return true;
}

// Test: Pass flags
bool TestRenderGraph_PassFlags()
{
    LOG(Info, "TestRenderGraph: Pass flags");

    auto* rasterPass = New<TestRasterPass>(TEXT("RasterPass"));
    auto* computePass = New<TestComputePass>(TEXT("ComputePass"));
    auto* copyPass = New<TestCopyPass>(TEXT("CopyPass"));

    if (!rasterPass->IsRaster())
    {
        LOG(Error, "Raster pass should have raster flag");
        Delete(rasterPass);
        Delete(computePass);
        Delete(copyPass);
        return false;
    }

    if (!computePass->IsCompute())
    {
        LOG(Error, "Compute pass should have compute flag");
        Delete(rasterPass);
        Delete(computePass);
        Delete(copyPass);
        return false;
    }

    if (!copyPass->IsCopy())
    {
        LOG(Error, "Copy pass should have copy flag");
        Delete(rasterPass);
        Delete(computePass);
        Delete(copyPass);
        return false;
    }

    Delete(rasterPass);
    Delete(computePass);
    Delete(copyPass);

    LOG(Info, "TestRenderGraph: Pass flags - PASSED");
    return true;
}

// Test: Resource aliasing
bool TestRenderGraph_ResourceAliasing()
{
    LOG(Info, "TestRenderGraph: Resource aliasing");

    MockRenderGraph graph;

    // Create two textures with the same description
    RenderGraphTextureDesc desc;
    desc.Name = TEXT("Texture1");
    desc.Width = 1920;
    desc.Height = 1080;
    desc.Format = PixelFormat::R8G8B8A8_UNorm;
    desc.Flags = GPUTextureFlags::RenderTarget;

    auto tex1 = graph.CreateTexture(desc);
    
    desc.Name = TEXT("Texture2");
    auto tex2 = graph.CreateTexture(desc);

    // Both should be valid but different references
    if (!tex1.IsValid() || !tex2.IsValid())
    {
        LOG(Error, "Failed to create textures");
        return false;
    }

    if (tex1.Index == tex2.Index)
    {
        LOG(Error, "Textures should have different indices");
        return false;
    }

    LOG(Info, "TestRenderGraph: Resource aliasing - PASSED");
    return true;
}

// Test: External resource import
bool TestRenderGraph_ExternalResources()
{
    LOG(Info, "TestRenderGraph: External resource import");

    MockRenderGraph graph;

    // Import external texture (nullptr for testing)
    auto texRef = graph.ImportTexture(TEXT("ExternalTexture"), nullptr);
    if (!texRef.IsValid())
    {
        LOG(Error, "Failed to import external texture");
        return false;
    }

    // Import external buffer (nullptr for testing)
    auto bufRef = graph.ImportBuffer(TEXT("ExternalBuffer"), nullptr);
    if (!bufRef.IsValid())
    {
        LOG(Error, "Failed to import external buffer");
        return false;
    }

    LOG(Info, "TestRenderGraph: External resource import - PASSED");
    return true;
}

// Main test runner
void TestRenderGraph()
{
    LOG(Info, "=== Running RenderGraph Unit Tests ===");

    int32 passed = 0;
    int32 failed = 0;

    if (TestRenderGraph_BasicPassCreation()) passed++; else failed++;
    if (TestRenderGraph_MultiplePasses()) passed++; else failed++;
    if (TestRenderGraph_ResourceManagement()) passed++; else failed++;
    if (TestRenderGraph_PassCulling()) passed++; else failed++;
    if (TestRenderGraph_PassFlags()) passed++; else failed++;
    if (TestRenderGraph_ResourceAliasing()) passed++; else failed++;
    if (TestRenderGraph_ExternalResources()) passed++; else failed++;

    LOG(Info, "=== RenderGraph Unit Tests Complete ===");
    LOG(Info, "Passed: {0}, Failed: {1}", passed, failed);

    if (failed > 0)
    {
        LOG(Warning, "Some RenderGraph tests failed!");
    }
}

#endif
