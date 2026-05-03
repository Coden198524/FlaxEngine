// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

class GPUTexture;

/// <summary>
/// Structure that contains precomputed data for atmosphere rendering.
/// </summary>
struct AtmosphereCache
{
    GPUTexture* Transmittance;
    GPUTexture* Irradiance;
    GPUTexture* Inscatter;
};

/// <summary>
/// PBR atmosphere cache data rendering service.
/// Note: This is an independent precomputation service that runs asynchronously
/// and caches results across multiple frames. It does not need to be integrated
/// into the RenderGraph architecture as it's not part of the per-frame rendering pipeline.
/// Other passes can access the precomputed data via GetCache().
/// </summary>
class FLAXENGINE_API AtmospherePreCompute
{
public:

    /// <summary>
    /// Gets the atmosphere cache textures.
    /// </summary>
    /// <param name="cache">Result cache</param>
    /// <returns>True if context is ready for usage.</returns>
    static bool GetCache(AtmosphereCache* cache);
};
