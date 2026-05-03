// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphCompiler.h"
#include "RenderGraphTypes.h"
#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Collections/Dictionary.h"
#include "Engine/Graphics/Textures/GPUTextureDescription.h"
#include "Engine/Graphics/GPUBufferDescription.h"

// Forward declarations
class RenderGraph;
class GPUTexture;
class GPUBuffer;

/// <summary>
/// Render graph resource manager that handles texture and buffer lifecycle management,
/// resource pooling, aliasing allocation, and memory optimization.
/// </summary>
class FLAXENGINE_API RenderGraphResourceManager
{
    friend class RenderGraph;

public:
    /// <summary>
    /// Pooled texture entry for resource reuse.
    /// </summary>
    struct PooledTexture
    {
        /// <summary>
        /// The GPU texture.
        /// </summary>
        GPUTexture* Texture;

        /// <summary>
        /// The texture description hash for fast matching.
        /// </summary>
        uint32 DescriptionHash;

        /// <summary>
        /// The frame when this texture was last used.
        /// </summary>
        uint64 LastFrameUsed;

        /// <summary>
        /// Whether this texture is currently in use.
        /// </summary>
        bool InUse;

        PooledTexture()
            : Texture(nullptr)
            , DescriptionHash(0)
            , LastFrameUsed(0)
            , InUse(false)
        {
        }
    };

    /// <summary>
    /// Pooled buffer entry for resource reuse.
    /// </summary>
    struct PooledBuffer
    {
        /// <summary>
        /// The GPU buffer.
        /// </summary>
        GPUBuffer* Buffer;

        /// <summary>
        /// The buffer description hash for fast matching.
        /// </summary>
        uint32 DescriptionHash;

        /// <summary>
        /// The frame when this buffer was last used.
        /// </summary>
        uint64 LastFrameUsed;

        /// <summary>
        /// Whether this buffer is currently in use.
        /// </summary>
        bool InUse;

        PooledBuffer()
            : Buffer(nullptr)
            , DescriptionHash(0)
            , LastFrameUsed(0)
            , InUse(false)
        {
        }
    };

    /// <summary>
    /// Resource aliasing information for memory optimization.
    /// </summary>
    struct AliasingInfo
    {
        /// <summary>
        /// Index of the resource that this resource aliases with (-1 if no aliasing).
        /// </summary>
        int32 AliasedResourceIndex;

        /// <summary>
        /// Whether this resource can be aliased.
        /// </summary>
        bool CanAlias;

        AliasingInfo()
            : AliasedResourceIndex(-1)
            , CanAlias(true)
        {
        }
    };

private:
    /// <summary>
    /// The render graph that owns this resource manager.
    /// </summary>
    RenderGraph* _graph;

    /// <summary>
    /// Pool of available textures for reuse.
    /// </summary>
    Array<PooledTexture> _texturePool;

    /// <summary>
    /// Pool of available buffers for reuse.
    /// </summary>
    Array<PooledBuffer> _bufferPool;

    /// <summary>
    /// Aliasing information for texture resources.
    /// </summary>
    Array<AliasingInfo> _textureAliasing;

    /// <summary>
    /// Aliasing information for buffer resources.
    /// </summary>
    Array<AliasingInfo> _bufferAliasing;

    /// <summary>
    /// Total memory allocated for textures (in bytes).
    /// </summary>
    uint64 _textureMemoryAllocated;

    /// <summary>
    /// Total memory allocated for buffers (in bytes).
    /// </summary>
    uint64 _bufferMemoryAllocated;

    /// <summary>
    /// Number of textures allocated this frame.
    /// </summary>
    int32 _texturesAllocatedThisFrame;

    /// <summary>
    /// Number of buffers allocated this frame.
    /// </summary>
    int32 _buffersAllocatedThisFrame;

    /// <summary>
    /// Number of textures reused from pool this frame.
    /// </summary>
    int32 _texturesReusedThisFrame;

    /// <summary>
    /// Number of buffers reused from pool this frame.
    /// </summary>
    int32 _buffersReusedThisFrame;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphResourceManager"/> class.
    /// </summary>
    /// <param name="graph">The render graph that owns this manager.</param>
    RenderGraphResourceManager(RenderGraph* graph);

    /// <summary>
    /// Finalizes an instance of the <see cref="RenderGraphResourceManager"/> class.
    /// </summary>
    ~RenderGraphResourceManager();

public:
    /// <summary>
    /// Allocates a texture resource for the given description.
    /// Will reuse from pool if available, otherwise creates a new texture.
    /// </summary>
    /// <param name="desc">The render graph texture description.</param>
    /// <param name="lifetime">The resource lifetime information.</param>
    /// <returns>The allocated GPU texture.</returns>
    GPUTexture* AllocateTexture(const RenderGraphTextureDesc& desc, const RenderGraphCompiler::ResourceLifetime& lifetime);

    /// <summary>
    /// Allocates a buffer resource for the given description.
    /// Will reuse from pool if available, otherwise creates a new buffer.
    /// </summary>
    /// <param name="desc">The render graph buffer description.</param>
    /// <param name="lifetime">The resource lifetime information.</param>
    /// <returns>The allocated GPU buffer.</returns>
    GPUBuffer* AllocateBuffer(const RenderGraphBufferDesc& desc, const RenderGraphCompiler::ResourceLifetime& lifetime);

    /// <summary>
    /// Releases a texture resource back to the pool for reuse.
    /// </summary>
    /// <param name="texture">The texture to release.</param>
    void ReleaseTexture(GPUTexture* texture);

    /// <summary>
    /// Releases a buffer resource back to the pool for reuse.
    /// </summary>
    /// <param name="buffer">The buffer to release.</param>
    void ReleaseBuffer(GPUBuffer* buffer);

    /// <summary>
    /// Sets up resource aliasing information for memory optimization.
    /// Should be called after compilation with lifetime analysis results.
    /// </summary>
    /// <param name="textureCount">The number of texture resources.</param>
    /// <param name="bufferCount">The number of buffer resources.</param>
    void SetupAliasing(int32 textureCount, int32 bufferCount);

    /// <summary>
    /// Sets aliasing information for a specific texture resource.
    /// </summary>
    /// <param name="resourceIndex">The texture resource index.</param>
    /// <param name="aliasedResourceIndex">The index of the resource to alias with (-1 for no aliasing).</param>
    void SetTextureAliasing(int32 resourceIndex, int32 aliasedResourceIndex);

    /// <summary>
    /// Sets aliasing information for a specific buffer resource.
    /// </summary>
    /// <param name="resourceIndex">The buffer resource index.</param>
    /// <param name="aliasedResourceIndex">The index of the resource to alias with (-1 for no aliasing).</param>
    void SetBufferAliasing(int32 resourceIndex, int32 aliasedResourceIndex);

    /// <summary>
    /// Gets the aliased texture for a given resource index.
    /// </summary>
    /// <param name="resourceIndex">The texture resource index.</param>
    /// <returns>The aliased resource index, or -1 if no aliasing.</returns>
    int32 GetTextureAliasing(int32 resourceIndex) const;

    /// <summary>
    /// Gets the aliased buffer for a given resource index.
    /// </summary>
    /// <param name="resourceIndex">The buffer resource index.</param>
    /// <returns>The aliased resource index, or -1 if no aliasing.</returns>
    int32 GetBufferAliasing(int32 resourceIndex) const;

    /// <summary>
    /// Releases all unused resources from the pool.
    /// Resources not used for several frames will be deleted.
    /// </summary>
    /// <param name="force">If true, releases all resources immediately.</param>
    /// <param name="framesOffset">Number of frames to keep resources cached.</param>
    void ReleaseUnusedResources(bool force = false, int32 framesOffset = 180);

    /// <summary>
    /// Clears all resources and resets the manager state.
    /// </summary>
    void Clear();

    /// <summary>
    /// Resets frame statistics.
    /// Should be called at the beginning of each frame.
    /// </summary>
    void ResetFrameStats();

    /// <summary>
    /// Gets the total memory allocated for textures (in bytes).
    /// </summary>
    FORCE_INLINE uint64 GetTextureMemoryAllocated() const
    {
        return _textureMemoryAllocated;
    }

    /// <summary>
    /// Gets the total memory allocated for buffers (in bytes).
    /// </summary>
    FORCE_INLINE uint64 GetBufferMemoryAllocated() const
    {
        return _bufferMemoryAllocated;
    }

    /// <summary>
    /// Gets the number of textures in the pool.
    /// </summary>
    FORCE_INLINE int32 GetTexturePoolSize() const
    {
        return _texturePool.Count();
    }

    /// <summary>
    /// Gets the number of buffers in the pool.
    /// </summary>
    FORCE_INLINE int32 GetBufferPoolSize() const
    {
        return _bufferPool.Count();
    }

    /// <summary>
    /// Gets the number of textures allocated this frame.
    /// </summary>
    FORCE_INLINE int32 GetTexturesAllocatedThisFrame() const
    {
        return _texturesAllocatedThisFrame;
    }

    /// <summary>
    /// Gets the number of buffers allocated this frame.
    /// </summary>
    FORCE_INLINE int32 GetBuffersAllocatedThisFrame() const
    {
        return _buffersAllocatedThisFrame;
    }

    /// <summary>
    /// Gets the number of textures reused from pool this frame.
    /// </summary>
    FORCE_INLINE int32 GetTexturesReusedThisFrame() const
    {
        return _texturesReusedThisFrame;
    }

    /// <summary>
    /// Gets the number of buffers reused from pool this frame.
    /// </summary>
    FORCE_INLINE int32 GetBuffersReusedThisFrame() const
    {
        return _buffersReusedThisFrame;
    }

private:
    /// <summary>
    /// Finds a pooled texture matching the given description.
    /// </summary>
    /// <param name="desc">The texture description.</param>
    /// <param name="descHash">The description hash.</param>
    /// <returns>The pooled texture index, or -1 if not found.</returns>
    int32 FindPooledTexture(const GPUTextureDescription& desc, uint32 descHash);

    /// <summary>
    /// Finds a pooled buffer matching the given description.
    /// </summary>
    /// <param name="desc">The buffer description.</param>
    /// <param name="descHash">The description hash.</param>
    /// <returns>The pooled buffer index, or -1 if not found.</returns>
    int32 FindPooledBuffer(const GPUBufferDescription& desc, uint32 descHash);

    /// <summary>
    /// Calculates memory size for a texture.
    /// </summary>
    /// <param name="desc">The texture description.</param>
    /// <returns>The memory size in bytes.</returns>
    uint64 CalculateTextureMemorySize(const GPUTextureDescription& desc) const;

    /// <summary>
    /// Calculates memory size for a buffer.
    /// </summary>
    /// <param name="desc">The buffer description.</param>
    /// <returns>The memory size in bytes.</returns>
    uint64 CalculateBufferMemorySize(const GPUBufferDescription& desc) const;
};
