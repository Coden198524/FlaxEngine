// Copyright (c) Wojciech Figat. All rights reserved.

#include "RenderGraphResourceManager.h"
#include "RenderGraph.h"
#include "RenderGraphCompiler.h"
#include "Engine/Graphics/GPUDevice.h"
#include "Engine/Graphics/PixelFormatExtensions.h"
#include "Engine/Graphics/Textures/GPUTexture.h"
#include "Engine/Graphics/GPUBuffer.h"
#include "Engine/Core/Log.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Profiler/ProfilerCPU.h"

RenderGraphResourceManager::RenderGraphResourceManager(RenderGraph* graph)
    : _graph(graph)
    , _textureMemoryAllocated(0)
    , _bufferMemoryAllocated(0)
    , _texturesAllocatedThisFrame(0)
    , _buffersAllocatedThisFrame(0)
    , _texturesReusedThisFrame(0)
    , _buffersReusedThisFrame(0)
{
}

RenderGraphResourceManager::~RenderGraphResourceManager()
{
    Clear();
}

GPUTexture* RenderGraphResourceManager::AllocateTexture(const RenderGraphTextureDesc& desc, const RenderGraphCompiler::ResourceLifetime& lifetime)
{
    PROFILE_CPU_NAMED("AllocateTexture");

    const GPUTextureDescription& gpuDesc = desc.Desc;
    const String& name = desc.Name;
    const uint32 descHash = GetHash(gpuDesc);

    // Try to find a matching texture in the pool
    const int32 poolIndex = FindPooledTexture(gpuDesc, descHash);
    if (poolIndex >= 0)
    {
        auto& pooled = _texturePool[poolIndex];
        pooled.InUse = true;
        pooled.LastFrameUsed = Engine::FrameCount;
        _texturesReusedThisFrame++;

#if GPU_ENABLE_RESOURCE_NAMING
        if (name.HasChars())
            pooled.Texture->SetName(name);
#endif

        return pooled.Texture;
    }

    // Create a new texture
    const String textureName = name.HasChars() ? name : String::Format(TEXT("RenderGraph_Texture_{0}"), _texturePool.Count());
    GPUTexture* texture = GPUDevice::Instance->CreateTexture(textureName);
    if (texture->Init(gpuDesc))
    {
        Delete(texture);
        LOG(Error, "Failed to create render graph texture: {0}", name);
        return nullptr;
    }

    // Add to pool
    PooledTexture pooled;
    pooled.Texture = texture;
    pooled.DescriptionHash = descHash;
    pooled.LastFrameUsed = Engine::FrameCount;
    pooled.InUse = true;
    _texturePool.Add(pooled);

    // Update statistics
    const uint64 memorySize = CalculateTextureMemorySize(gpuDesc);
    _textureMemoryAllocated += memorySize;
    _texturesAllocatedThisFrame++;

    return texture;
}

GPUBuffer* RenderGraphResourceManager::AllocateBuffer(const RenderGraphBufferDesc& desc, const RenderGraphCompiler::ResourceLifetime& lifetime)
{
    PROFILE_CPU_NAMED("AllocateBuffer");

    const GPUBufferDescription& gpuDesc = desc.Desc;
    const String& name = desc.Name;
    const uint32 descHash = GetHash(gpuDesc);

    // Try to find a matching buffer in the pool
    const int32 poolIndex = FindPooledBuffer(gpuDesc, descHash);
    if (poolIndex >= 0)
    {
        auto& pooled = _bufferPool[poolIndex];
        pooled.InUse = true;
        pooled.LastFrameUsed = Engine::FrameCount;
        _buffersReusedThisFrame++;

#if GPU_ENABLE_RESOURCE_NAMING
        if (name.HasChars())
            pooled.Buffer->SetName(name);
#endif

        return pooled.Buffer;
    }

    // Create a new buffer
    const String bufferName = name.HasChars() ? name : String::Format(TEXT("RenderGraph_Buffer_{0}"), _bufferPool.Count());
    GPUBuffer* buffer = GPUDevice::Instance->CreateBuffer(bufferName);
    if (buffer->Init(gpuDesc))
    {
        Delete(buffer);
        LOG(Error, "Failed to create render graph buffer: {0}", name);
        return nullptr;
    }

    // Add to pool
    PooledBuffer pooled;
    pooled.Buffer = buffer;
    pooled.DescriptionHash = descHash;
    pooled.LastFrameUsed = Engine::FrameCount;
    pooled.InUse = true;
    _bufferPool.Add(pooled);

    // Update statistics
    const uint64 memorySize = CalculateBufferMemorySize(gpuDesc);
    _bufferMemoryAllocated += memorySize;
    _buffersAllocatedThisFrame++;

    return buffer;
}

void RenderGraphResourceManager::ReleaseTexture(GPUTexture* texture)
{
    if (!texture)
        return;

    // Find the texture in the pool and mark as not in use
    for (int32 i = 0; i < _texturePool.Count(); i++)
    {
        if (_texturePool[i].Texture == texture)
        {
            _texturePool[i].InUse = false;
            _texturePool[i].LastFrameUsed = Engine::FrameCount;
            return;
        }
    }
}

void RenderGraphResourceManager::ReleaseBuffer(GPUBuffer* buffer)
{
    if (!buffer)
        return;

    // Find the buffer in the pool and mark as not in use
    for (int32 i = 0; i < _bufferPool.Count(); i++)
    {
        if (_bufferPool[i].Buffer == buffer)
        {
            _bufferPool[i].InUse = false;
            _bufferPool[i].LastFrameUsed = Engine::FrameCount;
            return;
        }
    }
}

void RenderGraphResourceManager::SetupAliasing(int32 textureCount, int32 bufferCount)
{
    _textureAliasing.Resize(textureCount);
    _bufferAliasing.Resize(bufferCount);

    // Initialize aliasing info
    for (int32 i = 0; i < textureCount; i++)
    {
        _textureAliasing[i].AliasedResourceIndex = -1;
        _textureAliasing[i].CanAlias = true;
    }

    for (int32 i = 0; i < bufferCount; i++)
    {
        _bufferAliasing[i].AliasedResourceIndex = -1;
        _bufferAliasing[i].CanAlias = true;
    }
}

void RenderGraphResourceManager::SetTextureAliasing(int32 resourceIndex, int32 aliasedResourceIndex)
{
    if (resourceIndex >= 0 && resourceIndex < _textureAliasing.Count())
    {
        _textureAliasing[resourceIndex].AliasedResourceIndex = aliasedResourceIndex;
    }
}

void RenderGraphResourceManager::SetBufferAliasing(int32 resourceIndex, int32 aliasedResourceIndex)
{
    if (resourceIndex >= 0 && resourceIndex < _bufferAliasing.Count())
    {
        _bufferAliasing[resourceIndex].AliasedResourceIndex = aliasedResourceIndex;
    }
}

int32 RenderGraphResourceManager::GetTextureAliasing(int32 resourceIndex) const
{
    if (resourceIndex >= 0 && resourceIndex < _textureAliasing.Count())
    {
        return _textureAliasing[resourceIndex].AliasedResourceIndex;
    }
    return -1;
}

int32 RenderGraphResourceManager::GetBufferAliasing(int32 resourceIndex) const
{
    if (resourceIndex >= 0 && resourceIndex < _bufferAliasing.Count())
    {
        return _bufferAliasing[resourceIndex].AliasedResourceIndex;
    }
    return -1;
}

void RenderGraphResourceManager::ReleaseUnusedResources(bool force, int32 framesOffset)
{
    PROFILE_CPU_NAMED("ReleaseUnusedResources");

    const uint64 frameCount = Engine::FrameCount;
    const uint64 maxReleaseFrame = frameCount - Math::Min<uint64>(frameCount, framesOffset);
    force |= Engine::ShouldExit();

    uint64 textureMemoryReleased = 0;
    uint64 bufferMemoryReleased = 0;
    int32 texturesReleased = 0;
    int32 buffersReleased = 0;

    // Release unused textures
    for (int32 i = 0; i < _texturePool.Count(); i++)
    {
        auto& pooled = _texturePool[i];
        if (!pooled.InUse && (force || pooled.LastFrameUsed < maxReleaseFrame))
        {
            const uint64 memorySize = CalculateTextureMemorySize(pooled.Texture->GetDescription());
            textureMemoryReleased += memorySize;
            _textureMemoryAllocated -= memorySize;
            texturesReleased++;

            pooled.Texture->DeleteObjectNow();
            _texturePool.RemoveAt(i--);

            if (_texturePool.IsEmpty())
                break;
        }
    }

    // Release unused buffers
    for (int32 i = 0; i < _bufferPool.Count(); i++)
    {
        auto& pooled = _bufferPool[i];
        if (!pooled.InUse && (force || pooled.LastFrameUsed < maxReleaseFrame))
        {
            const uint64 memorySize = CalculateBufferMemorySize(pooled.Buffer->GetDescription());
            bufferMemoryReleased += memorySize;
            _bufferMemoryAllocated -= memorySize;
            buffersReleased++;

            pooled.Buffer->DeleteObjectNow();
            _bufferPool.RemoveAt(i--);

            if (_bufferPool.IsEmpty())
                break;
        }
    }

    // Log statistics if resources were released
    if (texturesReleased > 0 || buffersReleased > 0)
    {
        LOG(Info, "RenderGraph released {0} textures ({1} MB) and {2} buffers ({3} MB)",
            texturesReleased, textureMemoryReleased / (1024 * 1024),
            buffersReleased, bufferMemoryReleased / (1024 * 1024));
    }
}

void RenderGraphResourceManager::Clear()
{
    PROFILE_CPU();

    // Release all textures
    for (auto& pooled : _texturePool)
    {
        if (pooled.Texture)
        {
            pooled.Texture->DeleteObjectNow();
        }
    }
    _texturePool.Clear();

    // Release all buffers
    for (auto& pooled : _bufferPool)
    {
        if (pooled.Buffer)
        {
            pooled.Buffer->DeleteObjectNow();
        }
    }
    _bufferPool.Clear();

    // Clear aliasing info
    _textureAliasing.Clear();
    _bufferAliasing.Clear();

    // Reset statistics
    _textureMemoryAllocated = 0;
    _bufferMemoryAllocated = 0;
    ResetFrameStats();
}

void RenderGraphResourceManager::ResetFrameStats()
{
    _texturesAllocatedThisFrame = 0;
    _buffersAllocatedThisFrame = 0;
    _texturesReusedThisFrame = 0;
    _buffersReusedThisFrame = 0;
}

int32 RenderGraphResourceManager::FindPooledTexture(const GPUTextureDescription& desc, uint32 descHash)
{
    // Search from the end for better cache locality and higher reuse probability
    // (recently released resources are more likely to be reused)
    for (int32 i = _texturePool.Count() - 1; i >= 0; i--)
    {
        const auto& pooled = _texturePool[i];
        if (!pooled.InUse && pooled.DescriptionHash == descHash)
        {
            // Verify the description matches (hash collision check)
            const auto& pooledDesc = pooled.Texture->GetDescription();
            if (pooledDesc.Width == desc.Width &&
                pooledDesc.Height == desc.Height &&
                pooledDesc.Depth == desc.Depth &&
                pooledDesc.ArraySize == desc.ArraySize &&
                pooledDesc.MipLevels == desc.MipLevels &&
                pooledDesc.Format == desc.Format &&
                pooledDesc.MultiSampleLevel == desc.MultiSampleLevel &&
                pooledDesc.Flags == desc.Flags &&
                pooledDesc.Dimensions == desc.Dimensions &&
                pooledDesc.Usage == desc.Usage)
            {
                return i;
            }
        }
    }

    return -1;
}

int32 RenderGraphResourceManager::FindPooledBuffer(const GPUBufferDescription& desc, uint32 descHash)
{
    // Search from the end for better cache locality and higher reuse probability
    for (int32 i = _bufferPool.Count() - 1; i >= 0; i--)
    {
        const auto& pooled = _bufferPool[i];
        if (!pooled.InUse && pooled.DescriptionHash == descHash)
        {
            // Verify the description matches (hash collision check)
            const auto& pooledDesc = pooled.Buffer->GetDescription();
            if (pooledDesc.Size == desc.Size &&
                pooledDesc.Stride == desc.Stride &&
                pooledDesc.Flags == desc.Flags &&
                pooledDesc.Format == desc.Format &&
                pooledDesc.Usage == desc.Usage)
            {
                return i;
            }
        }
    }

    return -1;
}

uint64 RenderGraphResourceManager::CalculateTextureMemorySize(const GPUTextureDescription& desc) const
{
    // Calculate approximate memory size based on texture properties
    uint64 size = 0;

    // Get bytes per pixel for the format
    const int32 bytesPerPixel = PixelFormatExtensions::SizeInBytes(desc.Format);

    // Calculate size for each mip level
    int32 width = desc.Width;
    int32 height = desc.Height;
    int32 depth = Math::Max(1, desc.Depth);

    for (int32 mip = 0; mip < desc.MipLevels; mip++)
    {
        const int32 mipWidth = Math::Max(1, width >> mip);
        const int32 mipHeight = Math::Max(1, height >> mip);
        const int32 mipDepth = Math::Max(1, depth >> mip);

        size += static_cast<uint64>(mipWidth) * mipHeight * mipDepth * bytesPerPixel;
    }

    // Multiply by array size
    size *= Math::Max(1, desc.ArraySize);

    // Account for multisampling
    if (desc.MultiSampleLevel > MSAALevel::None)
    {
        const int32 sampleCount = static_cast<int32>(desc.MultiSampleLevel);
        size *= sampleCount;
    }

    return size;
}

uint64 RenderGraphResourceManager::CalculateBufferMemorySize(const GPUBufferDescription& desc) const
{
    // Buffer memory size is simply the size field
    return desc.Size;
}
