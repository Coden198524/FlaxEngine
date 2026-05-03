// Copyright (c) Wojciech Figat. All rights reserved.

#include "RenderGraph.h"
#include "RenderGraphPass.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphExecutor.h"
#include "RenderGraphResourceManager.h"
#include "Engine/Graphics/GPUContext.h"
#include "Engine/Graphics/Textures/GPUTexture.h"
#include "Engine/Graphics/GPUBuffer.h"
#include "Engine/Graphics/RenderBuffers.h"
#include "Engine/Core/Log.h"
#include "Engine/Profiler/ProfilerCPU.h"
#include "Engine/Renderer/RenderList.h"

RenderGraph::RenderGraph()
    : _compiler(nullptr)
    , _executor(nullptr)
    , _resourceManager(nullptr)
    , _compiled(false)
    , _building(false)
    , _renderContext(nullptr)
    , _renderContextBatch(nullptr)
    , _currentSetupPass(nullptr)
{
    _compiler = New<RenderGraphCompiler>();
    _executor = New<RenderGraphExecutor>();
    _resourceManager = New<RenderGraphResourceManager>(this);
}

RenderGraph::~RenderGraph()
{
    Clear();
    
    if (_compiler)
    {
        Delete(_compiler);
        _compiler = nullptr;
    }
    
    if (_executor)
    {
        Delete(_executor);
        _executor = nullptr;
    }
    
    if (_resourceManager)
    {
        Delete(_resourceManager);
        _resourceManager = nullptr;
    }
}

int32 RenderGraph::AddPass(RenderGraphPass* pass, bool owned, bool neverCull)
{
    if (!pass)
        return -1;

    // Assign pass index
    pass->_passIndex = _passes.Count();
    
    // Add to pass list
    _passes.Add(pass);
    _ownsPasses.Add(owned);
    _neverCullPasses.Add(neverCull);
    
    // Mark as not compiled
    _compiled = false;
    
    return pass->_passIndex;
}

void RenderGraph::SetContext(RenderContext* renderContext, RenderContextBatch* renderContextBatch)
{
    _renderContext = renderContext;
    _renderContextBatch = renderContextBatch;
}

bool RenderGraph::Compile()
{
    PROFILE_CPU_NAMED("RenderGraph.Compile");

    if (_passes.IsEmpty())
    {
        LOG(Warning, "RenderGraph::Compile: No passes to compile");
        return false;
    }

    // Build dependencies by calling Setup on all passes
    {
        PROFILE_CPU_NAMED("RenderGraph.BuildDependencies");
        BuildDependencies();
    }

    // Compile the graph (pass culling, topological sort, resource lifetime analysis)
    {
        PROFILE_CPU_NAMED("RenderGraph.CompilerPass");
        if (!_compiler->Compile(this))
        {
            LOG(Error, "RenderGraph::Compile: Compilation failed");
            return false;
        }
    }

    // Allocate physical resources
    {
        PROFILE_CPU_NAMED("RenderGraph.AllocateResources");
        AllocateResources();
    }

    _compiled = true;
    return true;
}

bool RenderGraph::Execute(GPUContext* context)
{
    if (!context)
    {
        LOG(Error, "RenderGraph::Execute: Invalid GPU context");
        return false;
    }

    if (!_compiled)
    {
        LOG(Error, "RenderGraph::Execute: Graph not compiled");
        return false;
    }

    PROFILE_CPU_NAMED("RenderGraph.Execute");

    // Execute the graph
    if (!_executor->Execute(this, _compiler, context))
    {
        LOG(Error, "RenderGraph::Execute: Execution failed");
        return false;
    }

    return true;
}

void RenderGraph::Clear()
{
    // Release all allocated resources
    ReleaseResources();

    // Delete all passes
    for (int32 i = 0; i < _passes.Count(); i++)
    {
        if (_passes[i])
        {
            _passes[i]->_passIndex = -1;
            _passes[i]->_culled = false;
        }
        if (_passes[i] && i < _ownsPasses.Count() && _ownsPasses[i])
            Delete(_passes[i]);
    }
    _passes.Clear();
    _ownsPasses.Clear();
    _neverCullPasses.Clear();

    // Clear resource lists
    _textures.Clear();
    _textureNameToIndex.Clear();
    _buffers.Clear();
    _bufferNameToIndex.Clear();

    // Clear compiler and executor state
    if (_compiler)
        _compiler->Clear();
    if (_executor)
        _executor->Clear();
    if (_resourceManager)
        _resourceManager->Clear();

    _compiled = false;
    _building = false;
    _renderContext = nullptr;
    _renderContextBatch = nullptr;
    _currentSetupPass = nullptr;
}

RenderGraphTextureRef RenderGraph::CreateTexture(const RenderGraphTextureDesc& desc)
{
    TextureResource resource;
    resource.Desc = desc;
    resource.Texture = nullptr;
    resource.ProducerPass = -1;
    resource.IsImported = false;

    int32 index = _textures.Count();
    _textures.Add(resource);
    RegisterTextureName(desc.Name, index);

    return RenderGraphTextureRef(this, index);
}

RenderGraphTextureRef RenderGraph::ImportTexture(const String& name, GPUTexture* texture)
{
    if (!texture)
        return RenderGraphTextureRef();

    TextureResource resource;
    resource.Desc.Name = name;
    resource.Desc.Desc = texture->GetDescription();
    resource.Desc.Flags = RenderGraphResourceFlags::Imported | RenderGraphResourceFlags::Exported;
    resource.Texture = texture;
    resource.ProducerPass = -1;
    resource.IsImported = true;

    int32 index = _textures.Count();
    _textures.Add(resource);
    RegisterTextureName(name, index);

    return RenderGraphTextureRef(this, index);
}

RenderGraphBufferRef RenderGraph::CreateBuffer(const RenderGraphBufferDesc& desc)
{
    BufferResource resource;
    resource.Desc = desc;
    resource.Buffer = nullptr;
    resource.ProducerPass = -1;
    resource.IsImported = false;

    int32 index = _buffers.Count();
    _buffers.Add(resource);
    RegisterBufferName(desc.Name, index);

    return RenderGraphBufferRef(this, index);
}

RenderGraphBufferRef RenderGraph::ImportBuffer(const String& name, GPUBuffer* buffer)
{
    if (!buffer)
        return RenderGraphBufferRef();

    BufferResource resource;
    resource.Desc.Name = name;
    resource.Desc.Desc = buffer->GetDescription();
    resource.Desc.Flags = RenderGraphResourceFlags::Imported | RenderGraphResourceFlags::Exported;
    resource.Buffer = buffer;
    resource.ProducerPass = -1;
    resource.IsImported = true;

    int32 index = _buffers.Count();
    _buffers.Add(resource);
    RegisterBufferName(name, index);

    return RenderGraphBufferRef(this, index);
}

GPUTexture* RenderGraph::GetTexture(RenderGraphTextureRef handle)
{
    if (!handle.IsValid() || handle.Index >= _textures.Count() || (handle.Graph && handle.Graph != this))
        return nullptr;

    return _textures[handle.Index].Texture;
}

GPUTexture* RenderGraph::GetTexture(const String& name)
{
    return GetTexture(FindTexture(name));
}

GPUBuffer* RenderGraph::GetBuffer(RenderGraphBufferRef handle)
{
    if (!handle.IsValid() || handle.Index >= _buffers.Count() || (handle.Graph && handle.Graph != this))
        return nullptr;

    return _buffers[handle.Index].Buffer;
}

GPUBuffer* RenderGraph::GetBuffer(const String& name)
{
    return GetBuffer(FindBuffer(name));
}

RenderContext* RenderGraph::GetRenderContext() const
{
    return _renderContext;
}

RenderContextBatch* RenderGraph::GetRenderContextBatch() const
{
    return _renderContextBatch;
}

RenderGraphTextureRef RenderGraph::ReadTexture(const String& name, RenderGraphTextureAccess access)
{
    auto result = FindTexture(name);
    if (!result.IsValid())
        result = CreateNamedTexture(name, access);
    Read(result);
    return result;
}

RenderGraphTextureRef RenderGraph::WriteTexture(const String& name, RenderGraphTextureAccess access)
{
    auto result = FindTexture(name);
    if (!result.IsValid())
        result = CreateNamedTexture(name, access);
    Write(result);
    return result;
}

RenderGraphTextureRef RenderGraph::ReadWriteTexture(const String& name, RenderGraphTextureAccess access)
{
    auto result = FindTexture(name);
    if (!result.IsValid())
        result = CreateNamedTexture(name, access);
    Read(result);
    Write(result);
    return result;
}

RenderGraphBufferRef RenderGraph::ReadBuffer(const String& name, RenderGraphBufferAccess access)
{
    auto result = FindBuffer(name);
    if (!result.IsValid())
        result = CreateNamedBuffer(name);
    Read(result);
    return result;
}

RenderGraphBufferRef RenderGraph::WriteBuffer(const String& name, RenderGraphBufferAccess access)
{
    auto result = FindBuffer(name);
    if (!result.IsValid())
        result = CreateNamedBuffer(name);
    Write(result);
    return result;
}

RenderGraphBufferRef RenderGraph::ReadWriteBuffer(const String& name, RenderGraphBufferAccess access)
{
    auto result = FindBuffer(name);
    if (!result.IsValid())
        result = CreateNamedBuffer(name);
    Read(result);
    Write(result);
    return result;
}

void RenderGraph::Read(RenderGraphTextureRef texture)
{
    if (_currentSetupPass && texture.IsValid())
        _currentSetupPass->ReadTexture(texture);
}

void RenderGraph::Write(RenderGraphTextureRef texture)
{
    if (_currentSetupPass && texture.IsValid())
        _currentSetupPass->WriteTexture(texture);
}

void RenderGraph::Read(RenderGraphBufferRef buffer)
{
    if (_currentSetupPass && buffer.IsValid())
        _currentSetupPass->ReadBuffer(buffer);
}

void RenderGraph::Write(RenderGraphBufferRef buffer)
{
    if (_currentSetupPass && buffer.IsValid())
        _currentSetupPass->WriteBuffer(buffer);
}

RenderGraphTextureRef RenderGraph::FindTexture(const String& name) const
{
    int32 index;
    if (name.HasChars() && _textureNameToIndex.TryGet(name, index) && index >= 0 && index < _textures.Count())
        return RenderGraphTextureRef(const_cast<RenderGraph*>(this), index);
    return RenderGraphTextureRef();
}

RenderGraphBufferRef RenderGraph::FindBuffer(const String& name) const
{
    int32 index;
    if (name.HasChars() && _bufferNameToIndex.TryGet(name, index) && index >= 0 && index < _buffers.Count())
        return RenderGraphBufferRef(const_cast<RenderGraph*>(this), index);
    return RenderGraphBufferRef();
}

RenderGraphTextureRef RenderGraph::CreateNamedTexture(const String& name, RenderGraphTextureAccess access)
{
    if (!_renderContext || !_renderContext->Buffers || !name.HasChars())
        return RenderGraphTextureRef();

    auto buffers = _renderContext->Buffers;
    if (name == TEXT("GBuffer0") && buffers->GBuffer0)
        return ImportTexture(name, buffers->GBuffer0);
    if (name == TEXT("GBuffer1") && buffers->GBuffer1)
        return ImportTexture(name, buffers->GBuffer1);
    if (name == TEXT("GBuffer2") && buffers->GBuffer2)
        return ImportTexture(name, buffers->GBuffer2);
    if (name == TEXT("GBuffer3") && buffers->GBuffer3)
        return ImportTexture(name, buffers->GBuffer3);
    if (name == TEXT("DepthBuffer") && buffers->DepthBuffer)
        return ImportTexture(name, buffers->DepthBuffer);
    if (name == TEXT("MotionVectors") && buffers->MotionVectors)
        return ImportTexture(name, buffers->MotionVectors);
    if ((name == TEXT("InputFrame") || name == TEXT("ColorBuffer")) && buffers->GBuffer0)
        return ImportTexture(name, buffers->GBuffer0);

    auto flags = GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget;
    if (access == RenderGraphTextureAccess::UAV)
        flags |= GPUTextureFlags::UnorderedAccess;
    RenderGraphTextureDesc desc = RenderGraphTextureDesc::Create2D(buffers->GetWidth(), buffers->GetHeight(), buffers->GetOutputFormat(), flags, name);
    if (name == TEXT("OutputFrame"))
        desc.Flags |= RenderGraphResourceFlags::Exported;
    return CreateTexture(desc);
}

RenderGraphBufferRef RenderGraph::CreateNamedBuffer(const String& name)
{
    if (!name.HasChars())
        return RenderGraphBufferRef();
    LOG(Warning, "RenderGraph: missing named buffer '{0}'", name);
    return RenderGraphBufferRef();
}

void RenderGraph::RegisterTextureName(const String& name, int32 index)
{
    if (name.HasChars())
        _textureNameToIndex[name] = index;
}

void RenderGraph::RegisterBufferName(const String& name, int32 index)
{
    if (name.HasChars())
        _bufferNameToIndex[name] = index;
}

GPUTexture* RenderGraphTextureRef::GetTexture() const
{
    return Graph ? Graph->GetTexture(*this) : nullptr;
}

GPUBuffer* RenderGraphBufferRef::GetBuffer() const
{
    return Graph ? Graph->GetBuffer(*this) : nullptr;
}

void RenderGraph::BuildDependencies()
{
    _building = true;

    // Call Setup on all passes to declare their resource dependencies
    for (int32 i = 0; i < _passes.Count(); i++)
    {
        RenderGraphPass* pass = _passes[i];
        if (!pass)
            continue;

        // Clear previous dependencies
        pass->_textureReads.Clear();
        pass->_textureWrites.Clear();
        pass->_bufferReads.Clear();
        pass->_bufferWrites.Clear();
        pass->_culled = false;

        // Call Setup to declare dependencies
        _currentSetupPass = pass;
        pass->Setup(*this);
        _currentSetupPass = nullptr;
    }

    // Determine producer passes for each resource
    for (int32 i = 0; i < _passes.Count(); i++)
    {
        RenderGraphPass* pass = _passes[i];
        if (!pass)
            continue;

        // Mark this pass as producer for all textures it writes
        for (int32 j = 0; j < pass->_textureWrites.Count(); j++)
        {
            int32 texIndex = pass->_textureWrites[j].Index;
            if (texIndex >= 0 && texIndex < _textures.Count())
            {
                // First writer becomes the producer
                if (_textures[texIndex].ProducerPass == -1)
                    _textures[texIndex].ProducerPass = i;
            }
        }

        // Mark this pass as producer for all buffers it writes
        for (int32 j = 0; j < pass->_bufferWrites.Count(); j++)
        {
            int32 bufIndex = pass->_bufferWrites[j].Index;
            if (bufIndex >= 0 && bufIndex < _buffers.Count())
            {
                // First writer becomes the producer
                if (_buffers[bufIndex].ProducerPass == -1)
                    _buffers[bufIndex].ProducerPass = i;
            }
        }
    }

    _building = false;
}

void RenderGraph::AllocateResources()
{
    if (!_resourceManager)
        return;

    PROFILE_CPU_NAMED("RenderGraph.AllocateResources");

    // Allocate textures
    for (int32 i = 0; i < _textures.Count(); i++)
    {
        TextureResource& resource = _textures[i];

        // Skip imported resources (already have GPU texture)
        if (resource.IsImported)
            continue;

        // Skip if already allocated
        if (resource.Texture)
            continue;

        // Get lifetime information from compiler
        const auto& lifetime = _compiler->GetTextureLifetime(i);
        
        // Skip resources that are never used
        if (lifetime.FirstUse == -1 || lifetime.LastUse == -1)
            continue;

        // Allocate texture from resource manager
        resource.Texture = _resourceManager->AllocateTexture(resource.Desc, lifetime);
    }

    // Allocate buffers
    for (int32 i = 0; i < _buffers.Count(); i++)
    {
        BufferResource& resource = _buffers[i];

        // Skip imported resources (already have GPU buffer)
        if (resource.IsImported)
            continue;

        // Skip if already allocated
        if (resource.Buffer)
            continue;

        // Get lifetime information from compiler
        const auto& lifetime = _compiler->GetBufferLifetime(i);
        
        // Skip resources that are never used
        if (lifetime.FirstUse == -1 || lifetime.LastUse == -1)
            continue;

        // Allocate buffer from resource manager
        resource.Buffer = _resourceManager->AllocateBuffer(resource.Desc, lifetime);
    }
}

void RenderGraph::ReleaseResources()
{
    if (!_resourceManager)
        return;

    // Release textures (but not imported ones)
    for (int32 i = 0; i < _textures.Count(); i++)
    {
        TextureResource& resource = _textures[i];
        
        if (resource.Texture && !resource.IsImported)
        {
            _resourceManager->ReleaseTexture(resource.Texture);
            resource.Texture = nullptr;
        }
    }

    // Release buffers (but not imported ones)
    for (int32 i = 0; i < _buffers.Count(); i++)
    {
        BufferResource& resource = _buffers[i];
        
        if (resource.Buffer && !resource.IsImported)
        {
            _resourceManager->ReleaseBuffer(resource.Buffer);
            resource.Buffer = nullptr;
        }
    }

    // Release unused resources from the pool
    if (_resourceManager)
        _resourceManager->ReleaseUnusedResources();
}
