// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "Engine/Core/Types/String.h"
#include "Engine/Core/Collections/Array.h"

// Forward declarations
class RenderGraph;
class RenderGraphBuilder;
class GPUContext;

/// <summary>
/// Base class for render graph passes. Defines the interface for pass setup and execution.
/// </summary>
class FLAXENGINE_API RenderGraphPass
{
    friend class RenderGraph;
    friend class RenderGraphBuilder;
    friend class RenderGraphCompiler;
    friend class RenderGraphExecutor;

protected:
    /// <summary>
    /// The pass name (for debugging and profiling).
    /// </summary>
    String _name;

    /// <summary>
    /// The pass execution flags.
    /// </summary>
    RenderGraphPassFlags _flags;

    /// <summary>
    /// List of texture resources read by this pass.
    /// </summary>
    Array<RenderGraphTextureRef> _textureReads;

    /// <summary>
    /// List of texture resources written by this pass.
    /// </summary>
    Array<RenderGraphTextureRef> _textureWrites;

    /// <summary>
    /// List of buffer resources read by this pass.
    /// </summary>
    Array<RenderGraphBufferRef> _bufferReads;

    /// <summary>
    /// List of buffer resources written by this pass.
    /// </summary>
    Array<RenderGraphBufferRef> _bufferWrites;

    /// <summary>
    /// Whether this pass has been culled during compilation.
    /// </summary>
    bool _culled;

    /// <summary>
    /// Internal pass index in the render graph.
    /// </summary>
    int32 _passIndex;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphPass"/> class.
    /// </summary>
    /// <param name="name">The pass name.</param>
    /// <param name="flags">The pass flags.</param>
    RenderGraphPass(const String& name, RenderGraphPassFlags flags = RenderGraphPassFlags::Raster)
        : _name(name)
        , _flags(flags)
        , _culled(false)
        , _passIndex(-1)
    {
    }

    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~RenderGraphPass() = default;

public:
    /// <summary>
    /// Gets the pass name.
    /// </summary>
    FORCE_INLINE const String& GetName() const
    {
        return _name;
    }

    /// <summary>
    /// Gets the pass flags.
    /// </summary>
    FORCE_INLINE RenderGraphPassFlags GetFlags() const
    {
        return _flags;
    }

    /// <summary>
    /// Gets whether this pass has been culled.
    /// </summary>
    FORCE_INLINE bool IsCulled() const
    {
        return _culled;
    }

    /// <summary>
    /// Gets whether this pass is a raster pass.
    /// </summary>
    FORCE_INLINE bool IsRaster() const
    {
        return (_flags & RenderGraphPassFlags::Raster) != RenderGraphPassFlags::None;
    }

    /// <summary>
    /// Gets whether this pass is a compute pass.
    /// </summary>
    FORCE_INLINE bool IsCompute() const
    {
        return (_flags & RenderGraphPassFlags::Compute) != RenderGraphPassFlags::None;
    }

    /// <summary>
    /// Gets whether this pass is a copy pass.
    /// </summary>
    FORCE_INLINE bool IsCopy() const
    {
        return (_flags & RenderGraphPassFlags::Copy) != RenderGraphPassFlags::None;
    }

    /// <summary>
    /// Gets whether this pass can be culled.
    /// </summary>
    FORCE_INLINE bool CanCull() const
    {
        return (_flags & RenderGraphPassFlags::NeverCull) == RenderGraphPassFlags::None;
    }

public:
    /// <summary>
    /// Setup phase: declare resource dependencies and configure the pass.
    /// Called during graph building before compilation.
    /// </summary>
    /// <param name="builder">The render graph builder for declaring resources.</param>
    virtual void Setup(RenderGraphBuilder& builder) = 0;

    /// <summary>
    /// Execute phase: record GPU commands for this pass.
    /// Called during graph execution after compilation.
    /// </summary>
    /// <param name="context">The GPU context for recording commands.</param>
    virtual void Execute(GPUContext* context) = 0;

protected:
    /// <summary>
    /// Declares a texture resource as read input.
    /// </summary>
    /// <param name="texture">The texture reference.</param>
    void ReadTexture(RenderGraphTextureRef texture)
    {
        if (texture.IsValid())
            _textureReads.Add(texture);
    }

    /// <summary>
    /// Declares a texture resource as write output.
    /// </summary>
    /// <param name="texture">The texture reference.</param>
    void WriteTexture(RenderGraphTextureRef texture)
    {
        if (texture.IsValid())
            _textureWrites.Add(texture);
    }

    /// <summary>
    /// Declares a buffer resource as read input.
    /// </summary>
    /// <param name="buffer">The buffer reference.</param>
    void ReadBuffer(RenderGraphBufferRef buffer)
    {
        if (buffer.IsValid())
            _bufferReads.Add(buffer);
    }

    /// <summary>
    /// Declares a buffer resource as write output.
    /// </summary>
    /// <param name="buffer">The buffer reference.</param>
    void WriteBuffer(RenderGraphBufferRef buffer)
    {
        if (buffer.IsValid())
            _bufferWrites.Add(buffer);
    }
};

/// <summary>
/// Raster pass that renders to one or more render targets.
/// </summary>
class FLAXENGINE_API RenderGraphRasterPass : public RenderGraphPass
{
protected:
    /// <summary>
    /// Render target outputs.
    /// </summary>
    Array<RenderGraphTextureRef> _renderTargets;

    /// <summary>
    /// Depth-stencil target (optional).
    /// </summary>
    RenderGraphTextureRef _depthStencil;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphRasterPass"/> class.
    /// </summary>
    /// <param name="name">The pass name.</param>
    RenderGraphRasterPass(const String& name)
        : RenderGraphPass(name, RenderGraphPassFlags::Raster)
    {
    }

protected:
    /// <summary>
    /// Sets a render target output.
    /// </summary>
    /// <param name="index">The render target index.</param>
    /// <param name="texture">The texture reference.</param>
    void SetRenderTarget(int32 index, RenderGraphTextureRef texture)
    {
        if (index >= _renderTargets.Count())
            _renderTargets.Resize(index + 1);
        _renderTargets[index] = texture;
        WriteTexture(texture);
    }

    /// <summary>
    /// Sets the depth-stencil target.
    /// </summary>
    /// <param name="texture">The texture reference.</param>
    /// <param name="readOnly">True if depth is read-only.</param>
    void SetDepthStencil(RenderGraphTextureRef texture, bool readOnly = false)
    {
        _depthStencil = texture;
        if (readOnly)
            ReadTexture(texture);
        else
            WriteTexture(texture);
    }
};

/// <summary>
/// Compute pass that dispatches compute shaders.
/// </summary>
class FLAXENGINE_API RenderGraphComputePass : public RenderGraphPass
{
public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphComputePass"/> class.
    /// </summary>
    /// <param name="name">The pass name.</param>
    RenderGraphComputePass(const String& name)
        : RenderGraphPass(name, RenderGraphPassFlags::Compute)
    {
    }
};

/// <summary>
/// Copy pass that performs resource copies.
/// </summary>
class FLAXENGINE_API RenderGraphCopyPass : public RenderGraphPass
{
public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphCopyPass"/> class.
    /// </summary>
    /// <param name="name">The pass name.</param>
    RenderGraphCopyPass(const String& name)
        : RenderGraphPass(name, RenderGraphPassFlags::Copy)
    {
    }
};
