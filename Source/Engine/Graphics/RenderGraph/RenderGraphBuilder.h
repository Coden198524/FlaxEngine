// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "Engine/Core/Types/String.h"

// Forward declarations
class GPUTexture;
class GPUBuffer;

/// <summary>
/// Builder interface for declaring render graph resources during pass setup.
/// </summary>
class FLAXENGINE_API RenderGraphBuilder
{
public:
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

protected:
    virtual ~RenderGraphBuilder() = default;
};
