// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "Engine/Core/Types/String.h"

// Forward declarations
class GPUTexture;
class GPUBuffer;
struct RenderContext;
struct RenderContextBatch;

enum class RenderGraphTextureAccess
{
    SRV,
    RTV,
    UAV,
    RenderTarget = RTV,
};

enum class RenderGraphBufferAccess
{
    SRV,
    UAV,
};

/// <summary>
/// Builder interface for declaring render graph resources during pass setup.
/// </summary>
class FLAXENGINE_API RenderGraphBuilder
{
public:
    virtual RenderContext* GetRenderContext() const
    {
        return nullptr;
    }

    virtual RenderContextBatch* GetRenderContextBatch() const
    {
        return nullptr;
    }

    /// <summary>
    /// Creates a new texture resource in the render graph.
    /// </summary>
    /// <param name="desc">The texture description.</param>
    /// <returns>The texture reference.</returns>
    virtual RenderGraphTextureRef CreateTexture(const RenderGraphTextureDesc& desc) = 0;

    /// <summary>
    /// Imports an external texture into the render graph.
    /// </summary>
    /// <param name="name">The resource name.</param>
    /// <param name="texture">The external texture.</param>
    /// <returns>The texture reference.</returns>
    virtual RenderGraphTextureRef ImportTexture(const String& name, GPUTexture* texture) = 0;

    /// <summary>
    /// Creates a new buffer resource in the render graph.
    /// </summary>
    /// <param name="desc">The buffer description.</param>
    /// <returns>The buffer reference.</returns>
    virtual RenderGraphBufferRef CreateBuffer(const RenderGraphBufferDesc& desc) = 0;

    /// <summary>
    /// Imports an external buffer into the render graph.
    /// </summary>
    /// <param name="name">The resource name.</param>
    /// <param name="buffer">The external buffer.</param>
    /// <returns>The buffer reference.</returns>
    virtual RenderGraphBufferRef ImportBuffer(const String& name, GPUBuffer* buffer) = 0;

    /// <summary>
    /// Gets the actual GPU texture for a render graph texture reference (only valid during Execute).
    /// </summary>
    /// <param name="handle">The texture reference.</param>
    /// <returns>The GPU texture.</returns>
    virtual GPUTexture* GetTexture(RenderGraphTextureRef handle) = 0;

    /// <summary>
    /// Gets the actual GPU buffer for a render graph buffer reference (only valid during Execute).
    /// </summary>
    /// <param name="handle">The buffer reference.</param>
    /// <returns>The GPU buffer.</returns>
    virtual GPUBuffer* GetBuffer(RenderGraphBufferRef handle) = 0;

    virtual RenderGraphTextureRef ReadTexture(const String& name, RenderGraphTextureAccess access)
    {
        return RenderGraphTextureRef();
    }

    RenderGraphTextureRef ReadTexture(const char* name, RenderGraphTextureAccess access)
    {
        return ReadTexture(String(name), access);
    }

    RenderGraphTextureRef ReadTexture(const Char* name, RenderGraphTextureAccess access)
    {
        return ReadTexture(String(name), access);
    }

    virtual RenderGraphTextureRef WriteTexture(const String& name, RenderGraphTextureAccess access)
    {
        return RenderGraphTextureRef();
    }

    RenderGraphTextureRef WriteTexture(const char* name, RenderGraphTextureAccess access)
    {
        return WriteTexture(String(name), access);
    }

    RenderGraphTextureRef WriteTexture(const Char* name, RenderGraphTextureAccess access)
    {
        return WriteTexture(String(name), access);
    }

    virtual RenderGraphTextureRef ReadWriteTexture(const String& name, RenderGraphTextureAccess access)
    {
        return RenderGraphTextureRef();
    }

    RenderGraphTextureRef ReadWriteTexture(const char* name, RenderGraphTextureAccess access)
    {
        return ReadWriteTexture(String(name), access);
    }

    RenderGraphTextureRef ReadWriteTexture(const Char* name, RenderGraphTextureAccess access)
    {
        return ReadWriteTexture(String(name), access);
    }

    virtual RenderGraphBufferRef ReadBuffer(const String& name, RenderGraphBufferAccess access)
    {
        return RenderGraphBufferRef();
    }

    RenderGraphBufferRef ReadBuffer(const char* name, RenderGraphBufferAccess access)
    {
        return ReadBuffer(String(name), access);
    }

    RenderGraphBufferRef ReadBuffer(const Char* name, RenderGraphBufferAccess access)
    {
        return ReadBuffer(String(name), access);
    }

    virtual RenderGraphBufferRef WriteBuffer(const String& name, RenderGraphBufferAccess access)
    {
        return RenderGraphBufferRef();
    }

    RenderGraphBufferRef WriteBuffer(const char* name, RenderGraphBufferAccess access)
    {
        return WriteBuffer(String(name), access);
    }

    RenderGraphBufferRef WriteBuffer(const Char* name, RenderGraphBufferAccess access)
    {
        return WriteBuffer(String(name), access);
    }

    virtual RenderGraphBufferRef ReadWriteBuffer(const String& name, RenderGraphBufferAccess access)
    {
        return RenderGraphBufferRef();
    }

    RenderGraphBufferRef ReadWriteBuffer(const char* name, RenderGraphBufferAccess access)
    {
        return ReadWriteBuffer(String(name), access);
    }

    RenderGraphBufferRef ReadWriteBuffer(const Char* name, RenderGraphBufferAccess access)
    {
        return ReadWriteBuffer(String(name), access);
    }

    virtual void Read(RenderGraphTextureRef texture)
    {
    }

    virtual void Write(RenderGraphTextureRef texture)
    {
    }

    virtual void Read(RenderGraphBufferRef buffer)
    {
    }

    virtual void Write(RenderGraphBufferRef buffer)
    {
    }

    virtual void ReadTexture(RenderGraphTextureRef texture)
    {
        Read(texture);
    }

    virtual void WriteTexture(RenderGraphTextureRef texture)
    {
        Write(texture);
    }

protected:
    virtual ~RenderGraphBuilder() = default;
};
