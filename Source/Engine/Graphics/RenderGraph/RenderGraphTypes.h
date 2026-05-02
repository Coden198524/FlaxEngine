// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "Engine/Graphics/Textures/GPUTextureDescription.h"
#include "Engine/Graphics/GPUBufferDescription.h"
#include "Engine/Core/Types/String.h"

// Forward declarations
class GPUTexture;
class GPUBuffer;

/// <summary>
/// Render graph pass execution flags.
/// </summary>
API_ENUM(Attributes="Flags") enum class RenderGraphPassFlags
{
    /// <summary>
    /// No flags.
    /// </summary>
    None = 0x00,

    /// <summary>
    /// Pass never culled even if outputs are unused.
    /// </summary>
    NeverCull = 0x01,

    /// <summary>
    /// Pass can execute asynchronously on compute queue.
    /// </summary>
    Compute = 0x02,

    /// <summary>
    /// Pass can execute asynchronously on copy queue.
    /// </summary>
    Copy = 0x04,

    /// <summary>
    /// Pass is a raster pass (uses render targets).
    /// </summary>
    Raster = 0x08,
};

DECLARE_ENUM_OPERATORS(RenderGraphPassFlags);

/// <summary>
/// Render graph resource flags.
/// </summary>
API_ENUM(Attributes="Flags") enum class RenderGraphResourceFlags
{
    /// <summary>
    /// No flags.
    /// </summary>
    None = 0x00,

    /// <summary>
    /// Resource is imported from external source (not created by render graph).
    /// </summary>
    Imported = 0x01,

    /// <summary>
    /// Resource is exported and must persist after graph execution.
    /// </summary>
    Exported = 0x02,

    /// <summary>
    /// Resource can be aliased with other resources to save memory.
    /// </summary>
    AllowAliasing = 0x04,

    /// <summary>
    /// Resource should not be pooled (always create new).
    /// </summary>
    NoPooling = 0x08,
};

DECLARE_ENUM_OPERATORS(RenderGraphResourceFlags);

/// <summary>
/// Render graph resource access mode.
/// </summary>
API_ENUM() enum class RenderGraphResourceAccess
{
    /// <summary>
    /// Resource is read-only.
    /// </summary>
    Read = 0,

    /// <summary>
    /// Resource is write-only.
    /// </summary>
    Write = 1,

    /// <summary>
    /// Resource is read and written.
    /// </summary>
    ReadWrite = 2,
};

/// <summary>
/// Opaque handle to a render graph texture resource.
/// </summary>
struct RenderGraphTextureRef
{
    /// <summary>
    /// Internal resource index.
    /// </summary>
    int32 Index;

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphTextureRef"/> struct.
    /// </summary>
    RenderGraphTextureRef()
        : Index(-1)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphTextureRef"/> struct.
    /// </summary>
    /// <param name="index">The resource index.</param>
    explicit RenderGraphTextureRef(int32 index)
        : Index(index)
    {
    }

    /// <summary>
    /// Gets a value indicating whether this reference is valid.
    /// </summary>
    FORCE_INLINE bool IsValid() const
    {
        return Index >= 0;
    }

    /// <summary>
    /// Invalidates this reference.
    /// </summary>
    FORCE_INLINE void Invalidate()
    {
        Index = -1;
    }

    FORCE_INLINE bool operator==(const RenderGraphTextureRef& other) const
    {
        return Index == other.Index;
    }

    FORCE_INLINE bool operator!=(const RenderGraphTextureRef& other) const
    {
        return Index != other.Index;
    }
};

/// <summary>
/// Opaque handle to a render graph buffer resource.
/// </summary>
struct RenderGraphBufferRef
{
    /// <summary>
    /// Internal resource index.
    /// </summary>
    int32 Index;

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphBufferRef"/> struct.
    /// </summary>
    RenderGraphBufferRef()
        : Index(-1)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphBufferRef"/> struct.
    /// </summary>
    /// <param name="index">The resource index.</param>
    explicit RenderGraphBufferRef(int32 index)
        : Index(index)
    {
    }

    /// <summary>
    /// Gets a value indicating whether this reference is valid.
    /// </summary>
    FORCE_INLINE bool IsValid() const
    {
        return Index >= 0;
    }

    /// <summary>
    /// Invalidates this reference.
    /// </summary>
    FORCE_INLINE void Invalidate()
    {
        Index = -1;
    }

    FORCE_INLINE bool operator==(const RenderGraphBufferRef& other) const
    {
        return Index == other.Index;
    }

    FORCE_INLINE bool operator!=(const RenderGraphBufferRef& other) const
    {
        return Index != other.Index;
    }
};

/// <summary>
/// Description of a render graph texture resource.
/// </summary>
struct RenderGraphTextureDesc
{
    /// <summary>
    /// The texture description.
    /// </summary>
    GPUTextureDescription Desc;

    /// <summary>
    /// The resource name (for debugging).
    /// </summary>
    String Name;

    /// <summary>
    /// The resource flags.
    /// </summary>
    RenderGraphResourceFlags Flags;

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphTextureDesc"/> struct.
    /// </summary>
    RenderGraphTextureDesc()
        : Flags(RenderGraphResourceFlags::AllowAliasing)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphTextureDesc"/> struct.
    /// </summary>
    /// <param name="desc">The texture description.</param>
    /// <param name="name">The resource name.</param>
    /// <param name="flags">The resource flags.</param>
    RenderGraphTextureDesc(const GPUTextureDescription& desc, const String& name = String::Empty, RenderGraphResourceFlags flags = RenderGraphResourceFlags::AllowAliasing)
        : Desc(desc)
        , Name(name)
        , Flags(flags)
    {
    }

    /// <summary>
    /// Creates a 2D texture description.
    /// </summary>
    /// <param name="width">The width.</param>
    /// <param name="height">The height.</param>
    /// <param name="format">The format.</param>
    /// <param name="flags">The texture flags.</param>
    /// <param name="name">The resource name.</param>
    /// <returns>The texture description.</returns>
    static RenderGraphTextureDesc Create2D(int32 width, int32 height, PixelFormat format, GPUTextureFlags flags = GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget, const String& name = String::Empty)
    {
        return RenderGraphTextureDesc(GPUTextureDescription::New2D(width, height, format, flags), name);
    }

    /// <summary>
    /// Creates a 2D texture description with mip levels.
    /// </summary>
    /// <param name="width">The width.</param>
    /// <param name="height">The height.</param>
    /// <param name="mipLevels">The mip levels.</param>
    /// <param name="format">The format.</param>
    /// <param name="flags">The texture flags.</param>
    /// <param name="name">The resource name.</param>
    /// <returns>The texture description.</returns>
    static RenderGraphTextureDesc Create2D(int32 width, int32 height, int32 mipLevels, PixelFormat format, GPUTextureFlags flags = GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget, const String& name = String::Empty)
    {
        return RenderGraphTextureDesc(GPUTextureDescription::New2D(width, height, mipLevels, format, flags), name);
    }
};

/// <summary>
/// Description of a render graph buffer resource.
/// </summary>
struct RenderGraphBufferDesc
{
    /// <summary>
    /// The buffer description.
    /// </summary>
    GPUBufferDescription Desc;

    /// <summary>
    /// The resource name (for debugging).
    /// </summary>
    String Name;

    /// <summary>
    /// The resource flags.
    /// </summary>
    RenderGraphResourceFlags Flags;

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphBufferDesc"/> struct.
    /// </summary>
    RenderGraphBufferDesc()
        : Flags(RenderGraphResourceFlags::AllowAliasing)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphBufferDesc"/> struct.
    /// </summary>
    /// <param name="desc">The buffer description.</param>
    /// <param name="name">The resource name.</param>
    /// <param name="flags">The resource flags.</param>
    RenderGraphBufferDesc(const GPUBufferDescription& desc, const String& name = String::Empty, RenderGraphResourceFlags flags = RenderGraphResourceFlags::AllowAliasing)
        : Desc(desc)
        , Name(name)
        , Flags(flags)
    {
    }

    /// <summary>
    /// Creates a structured buffer description.
    /// </summary>
    /// <param name="elementCount">The element count.</param>
    /// <param name="elementSize">The element size.</param>
    /// <param name="isUnorderedAccess">True if unordered access is needed.</param>
    /// <param name="name">The resource name.</param>
    /// <returns>The buffer description.</returns>
    static RenderGraphBufferDesc CreateStructured(int32 elementCount, int32 elementSize, bool isUnorderedAccess = false, const String& name = String::Empty)
    {
        return RenderGraphBufferDesc(GPUBufferDescription::Structured(elementCount, elementSize, isUnorderedAccess), name);
    }
};
