// Copyright (c) Wojciech Figat. All rights reserved.

#if GRAPHICS_API_WEBGPU

#define WEBGPU_LOG_PSO 0
//#define WEBGPU_LOG_PSO_NAME "PS_GBuffer" // Debug log for PSOs with specific name
#define WEBGPU_LOG_BIND_GROUPS 0
#define WEBGPU_CACHE_BIND_GROUPS 1

#include "GPUPipelineStateWebGPU.h"
#include "GPUTextureWebGPU.h"
#include "GPUVertexLayoutWebGPU.h"
#include "RenderToolsWebGPU.h"
#include "Engine/Core/Log.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Profiler/ProfilerCPU.h"
#include "Engine/Profiler/ProfilerMemory.h"
#include "Engine/Graphics/PixelFormatExtensions.h"
#include "Engine/Utilities/Crc.h"
#if WEBGPU_LOG_PSO
#include "Engine/Scripting/Enums.h"
#endif

WGPUCompareFunction ToCompareFunction(ComparisonFunc value)
{
    switch (value)
    {
    case ComparisonFunc::Never:
        return WGPUCompareFunction_Never;
    case ComparisonFunc::Less:
        return WGPUCompareFunction_Less;
    case ComparisonFunc::Equal:
        return WGPUCompareFunction_Equal;
    case ComparisonFunc::LessEqual:
        return WGPUCompareFunction_LessEqual;
    case ComparisonFunc::Greater:
        return WGPUCompareFunction_Greater;
    case ComparisonFunc::NotEqual:
        return WGPUCompareFunction_NotEqual;
    case ComparisonFunc::GreaterEqual:
        return WGPUCompareFunction_GreaterEqual;
    case ComparisonFunc::Always:
        return WGPUCompareFunction_Always;
    default:
        return WGPUCompareFunction_Undefined;
    }
}

WGPUStencilOperation ToStencilOperation(StencilOperation value)
{
    switch (value)
    {
    case StencilOperation::Keep:
        return WGPUStencilOperation_Keep;
    case StencilOperation::Zero:
        return WGPUStencilOperation_Zero;
    case StencilOperation::Replace:
        return WGPUStencilOperation_Replace;
    case StencilOperation::IncrementSaturated:
        return WGPUStencilOperation_IncrementClamp;
    case StencilOperation::DecrementSaturated:
        return WGPUStencilOperation_DecrementClamp;
    case StencilOperation::Invert:
        return WGPUStencilOperation_Invert;
    case StencilOperation::Increment:
        return WGPUStencilOperation_IncrementWrap;
    case StencilOperation::Decrement:
        return WGPUStencilOperation_DecrementWrap;
    default:
        return WGPUStencilOperation_Undefined;
    }
}

bool HasStencilFormat(WGPUTextureFormat format)
{
    switch (format)
    {
    case WGPUTextureFormat_Depth24PlusStencil8:
    case WGPUTextureFormat_Depth32FloatStencil8:
        return true;
    default:
        return false;
    }
}

WGPUTextureSampleType ToTextureSampleType(PixelFormat format, bool float32Filterable, SpirvShaderResourceType resourceType)
{
    switch (format)
    {
    case PixelFormat::D16_UNorm:
    case PixelFormat::D24_UNorm_S8_UInt:
    case PixelFormat::D32_Float:
    case PixelFormat::D32_Float_S8X24_UInt:
        return WGPUTextureSampleType_Depth;
    case PixelFormat::R8_UInt:
    case PixelFormat::R16_UInt:
    case PixelFormat::R32_UInt:
    case PixelFormat::R8G8_UInt:
    case PixelFormat::R16G16_UInt:
    case PixelFormat::R32G32_UInt:
    case PixelFormat::R8G8B8A8_UInt:
    case PixelFormat::R16G16B16A16_UInt:
    case PixelFormat::R32G32B32A32_UInt:
        return WGPUTextureSampleType_Uint;
    case PixelFormat::R8_SInt:
    case PixelFormat::R16_SInt:
    case PixelFormat::R32_SInt:
    case PixelFormat::R8G8_SInt:
    case PixelFormat::R16G16_SInt:
    case PixelFormat::R32G32_SInt:
    case PixelFormat::R8G8B8A8_SInt:
    case PixelFormat::R16G16B16A16_SInt:
    case PixelFormat::R32G32B32A32_SInt:
        return WGPUTextureSampleType_Sint;
    case PixelFormat::R32_Float:
    case PixelFormat::R32G32_Float:
    case PixelFormat::R32G32B32A32_Float:
        // Some WebGPU backends expose float32-filterable for 2D textures but still validate 3D float textures as unfilterable.
        return float32Filterable && resourceType != SpirvShaderResourceType::Texture3D ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
    default:
        return WGPUTextureSampleType_Float;
    }
}

WGPUTextureSampleType GetTextureSampleType(bool float32Filterable, const GPUContextBindingsWebGPU& bindings, const SpirvShaderDescriptorInfo::Descriptor& descriptor)
{
    WGPUTextureSampleType sampleType = WGPUTextureSampleType_Undefined;
    if (bindings.ShaderResources && bindings.ShaderResources[descriptor.Slot])
    {
        // WebGPU validates sample type as part of the bind group layout, so prefer the actual view metadata.
        auto ptr = (GPUResourceViewPtrWebGPU*)bindings.ShaderResources[descriptor.Slot]->GetNativePtr();
        if (ptr && ptr->TextureView)
            sampleType = ptr->TextureView->SampleType;
    }
    return sampleType != WGPUTextureSampleType_Undefined ? sampleType : ToTextureSampleType(descriptor.ResourceFormat, float32Filterable, descriptor.ResourceType);
}

GPUComputePipelineKeyWebGPU GetComputePipelineKey(bool float32Filterable, const GPUContextBindingsWebGPU& bindings, const SpirvShaderDescriptorInfo& descriptors)
{
    GPUComputePipelineKeyWebGPU key;
    key.SampleTypesCount = (uint8)descriptors.DescriptorTypesCount;
    key.Hash = GetHash((uint32)key.SampleTypesCount);
    for (int32 index = 0; index < descriptors.DescriptorTypesCount; index++)
    {
        auto& descriptor = descriptors.DescriptorTypes[index];
        uint8 sampleType = 0;
        if (descriptor.DescriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
            sampleType = (uint8)GetTextureSampleType(float32Filterable, bindings, descriptor);
        key.SampleTypes[index] = sampleType;
        CombineHash(key.Hash, GetHash((uint32)sampleType));
    }
    return key;
}

WGPUBlendFactor ToBlendFactor(BlendingMode::Blend value)
{
    switch (value)
    {
    case BlendingMode::Blend::Zero:
        return WGPUBlendFactor_Zero;
    case BlendingMode::Blend::One:
        return WGPUBlendFactor_One;
    case BlendingMode::Blend::SrcColor:
        return WGPUBlendFactor_Src;
    case BlendingMode::Blend::InvSrcColor:
        return WGPUBlendFactor_OneMinusSrc;
    case BlendingMode::Blend::SrcAlpha:
        return WGPUBlendFactor_SrcAlpha;
    case BlendingMode::Blend::InvSrcAlpha:
        return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendingMode::Blend::DestAlpha:
        return WGPUBlendFactor_DstAlpha;
    case BlendingMode::Blend::InvDestAlpha:
        return WGPUBlendFactor_OneMinusDstAlpha;
    case BlendingMode::Blend::DestColor:
        return WGPUBlendFactor_Dst;
    case BlendingMode::Blend::InvDestColor:
        return WGPUBlendFactor_OneMinusDst;
    case BlendingMode::Blend::SrcAlphaSat:
        return WGPUBlendFactor_SrcAlphaSaturated;
    case BlendingMode::Blend::BlendFactor:
        return WGPUBlendFactor_Constant;
    case BlendingMode::Blend::BlendInvFactor:
        return WGPUBlendFactor_OneMinusConstant;
    case BlendingMode::Blend::Src1Color:
        return WGPUBlendFactor_Src1;
    case BlendingMode::Blend::InvSrc1Color:
        return WGPUBlendFactor_OneMinusSrc1;
    case BlendingMode::Blend::Src1Alpha:
        return WGPUBlendFactor_Src1Alpha;
    case BlendingMode::Blend::InvSrc1Alpha:
        return WGPUBlendFactor_OneMinusSrc1Alpha;
    default:
        return WGPUBlendFactor_Undefined;
    }
}

WGPUBlendComponent ToBlendComponent(BlendingMode::Operation blendOp, BlendingMode::Blend srcBlend, BlendingMode::Blend dstBlend)
{
    WGPUBlendComponent result;
    switch (blendOp)
    {
    case BlendingMode::Operation::Add:
        result.operation = WGPUBlendOperation_Add;
        break;
    case BlendingMode::Operation::Subtract:
        result.operation = WGPUBlendOperation_Subtract;
        break;
    case BlendingMode::Operation::RevSubtract:
        result.operation = WGPUBlendOperation_ReverseSubtract;
        break;
    case BlendingMode::Operation::Min:
        result.operation = WGPUBlendOperation_Min;
        break;
    case BlendingMode::Operation::Max:
        result.operation = WGPUBlendOperation_Max;
        break;
    default:
        result.operation = WGPUBlendOperation_Undefined;
        break;
    }
    result.srcFactor = ToBlendFactor(srcBlend);
    result.dstFactor = ToBlendFactor(dstBlend);
    return result;
}

typedef Array<WGPUBindGroupLayoutEntry, InlinedAllocation<16>> BindGroupEntries;

WGPUBindGroupLayout CreateBindGroupLayout(WGPUDevice device, bool float32Filterable, const GPUContextBindingsWebGPU& bindings, int32 groupIndex, const SpirvShaderDescriptorInfo& descriptors, BindGroupEntries& entries, const StringAnsiView& debugName, bool log, bool compute = false, WGPUSamplerBindingType* samplerTypes = nullptr)
{
    int32 entriesCount = descriptors.DescriptorTypesCount;
    if (entriesCount == 0)
        return nullptr;
    auto entriesPtr = entries.Get();
    ASSERT_LOW_LAYER(entries.Count() >= entriesCount);
    Platform::MemoryClear(entries.Get(), sizeof(WGPUBindGroupLayoutEntry) * entriesCount);
    auto visibility = compute ? WGPUShaderStage_Compute : (groupIndex == 0 ? WGPUShaderStage_Vertex : WGPUShaderStage_Fragment);
#if WEBGPU_LOG_PSO
    if (log)
        LOG(Info, " > group {} - {}", groupIndex, compute ? TEXT("Compute") : (groupIndex == 0 ? TEXT("Vertex") : TEXT("Fragment")));
    const Char* samplerType = TEXT("?");
#endif
    bool forceNonFilteringSamplers = false;
    if (samplerTypes)
        Platform::MemoryClear(samplerTypes, sizeof(WGPUSamplerBindingType) * GPU_MAX_SAMPLER_BINDED);
    for (int32 index = 0; index < entriesCount; index++)
    {
        auto& descriptor = descriptors.DescriptorTypes[index];
        if (descriptor.DescriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && GetTextureSampleType(float32Filterable, bindings, descriptor) == WGPUTextureSampleType_UnfilterableFloat)
        {
            forceNonFilteringSamplers = true;
            break;
        }
    }
    for (int32 index = 0; index < entriesCount; index++)
    {
        auto& descriptor = descriptors.DescriptorTypes[index];
        auto& entry = entriesPtr[index];
        entry.binding = descriptor.Binding;
        entry.bindingArraySize = descriptor.Count;
        entry.visibility = visibility;
        switch (descriptor.DescriptorType)
        {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            if (descriptor.Slot == 4 || descriptor.Slot == 5) // Hack for ShadowSampler and ShadowSamplerLinear (this could get binded samplers table just like for shaderResources)
                entry.sampler.type = WGPUSamplerBindingType_Comparison;
            else if (forceNonFilteringSamplers || descriptor.Slot == 1 || descriptor.Slot == 3)
                entry.sampler.type = WGPUSamplerBindingType_NonFiltering;
            else
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
            if (samplerTypes)
                samplerTypes[descriptor.Slot] = entry.sampler.type;
#if WEBGPU_LOG_PSO
            switch (entry.sampler.type)
            {
            case WGPUSamplerBindingType_BindingNotUsed:
                samplerType = TEXT("BindingNotUsed");
                break;
            case WGPUSamplerBindingType_Undefined:
                samplerType = TEXT("Undefined");
                break;
            case WGPUSamplerBindingType_Filtering:
                samplerType = TEXT("Filtering");
                break;
            case WGPUSamplerBindingType_NonFiltering:
                samplerType = TEXT("NonFiltering");
                break;
            case WGPUSamplerBindingType_Comparison:
                samplerType = TEXT("Comparison");
                break;
            }
            if (log)
                LOG(Info, "   > [{}] sampler ({})", entry.binding, samplerType);
#endif
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            entry.texture.sampleType = GetTextureSampleType(float32Filterable, bindings, descriptor);
#if WEBGPU_LOG_PSO
            if (log)
            {
                switch (entry.texture.sampleType)
                {
                case WGPUTextureSampleType_BindingNotUsed:
                    samplerType = TEXT("BindingNotUsed");
                    break;
                case WGPUTextureSampleType_Undefined:
                    samplerType = TEXT("Undefined");
                    break;
                case WGPUTextureSampleType_Float:
                    samplerType = TEXT("Float");
                    break;
                case WGPUTextureSampleType_UnfilterableFloat:
                    samplerType = TEXT("UnfilterableFloat");
                    break;
                case WGPUTextureSampleType_Depth:
                    samplerType = TEXT("Depth");
                    break;
                case WGPUTextureSampleType_Sint:
                    samplerType = TEXT("Sint");
                    break;
                case WGPUTextureSampleType_Uint:
                    samplerType = TEXT("Uint");
                    break;
                }
                switch (descriptor.ResourceType)
                {
                case SpirvShaderResourceType::Texture1D:
                    LOG(Info, "   > [{}] texture 1D ({})", entry.binding, samplerType);
                    break;
                case SpirvShaderResourceType::Texture2D:
                    LOG(Info, "   > [{}] texture 2D ({})", entry.binding, samplerType);
                    break;
                case SpirvShaderResourceType::Texture3D:
                    LOG(Info, "   > [{}] texture 3D ({})", entry.binding, samplerType);
                    break;
                case SpirvShaderResourceType::TextureCube:
                    LOG(Info, "   > [{}] texture Cube ({})", entry.binding, samplerType);
                    break;
                case SpirvShaderResourceType::Texture2DArray:
                    LOG(Info, "   > [{}] texture 2D array ({})", entry.binding, samplerType);
                    break;
                }
            }
#endif
            switch (descriptor.ResourceType)
            {
            case SpirvShaderResourceType::Texture1D:
                entry.texture.viewDimension = WGPUTextureViewDimension_1D;
                break;
            case SpirvShaderResourceType::Texture2D:
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case SpirvShaderResourceType::Texture3D:
                entry.texture.viewDimension = WGPUTextureViewDimension_3D;
                break;
            case SpirvShaderResourceType::TextureCube:
                entry.texture.viewDimension = WGPUTextureViewDimension_Cube;
                break;
            case SpirvShaderResourceType::Texture1DArray:
                CRASH; // Not supported TODO: add error at compile time (in ShaderCompilerWebGPU::Write)
                break;
            case SpirvShaderResourceType::Texture2DArray:
                entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;
                break;
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
            entry.storageTexture.format = RenderToolsWebGPU::ToTextureFormat(descriptor.ResourceFormat);
            switch (descriptor.ResourceType)
            {
            case SpirvShaderResourceType::Texture1D:
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_1D;
                break;
            case SpirvShaderResourceType::Texture2D:
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case SpirvShaderResourceType::Texture3D:
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_3D;
                break;
            case SpirvShaderResourceType::Texture2DArray:
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
                break;
            default:
                CRASH;
                break;
            }
#if WEBGPU_LOG_PSO
            if (log)
                LOG(Info, "   > [{}] storage texture ({})", entry.binding, String(debugName));
#endif
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            entry.buffer.hasDynamicOffset = true;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (descriptor.BindingType == SpirvShaderResourceBindingType::SRV)
                entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            else
                entry.buffer.type = WGPUBufferBindingType_Storage;
#if WEBGPU_LOG_PSO
            if (log)
                LOG(Info, "   > [{}] storage buffer (read-only = {}, dynamic = {})", entry.binding, entry.buffer.type == WGPUBufferBindingType_ReadOnlyStorage, entry.buffer.hasDynamicOffset);
#endif
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            entry.buffer.hasDynamicOffset = true;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            entry.buffer.type = WGPUBufferBindingType_Uniform;
#if WEBGPU_LOG_PSO
            if (log)
                LOG(Info, "   > [{}] uniform buffer (dynamic = {})", entry.binding, entry.buffer.hasDynamicOffset);
#endif
            break;
        default:
#if GPU_ENABLE_DIAGNOSTICS
            LOG(Fatal, "Unknown descriptor type: {} used as {} in '{}'", (uint32)descriptor.DescriptorType, (uint32)descriptor.BindingType, String(debugName));
#else
            CRASH;
#endif
            return nullptr;
        }
    }

    // Create a bind group layout
    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bindGroupLayoutDesc.entryCount = entriesCount;
    bindGroupLayoutDesc.entries = entriesPtr;
    return wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
}

WGPUComputePipeline GPUShaderProgramCSWebGPU::GetPipeline(WGPUDevice device, bool float32Filterable, const GPUContextBindingsWebGPU& bindings, WGPUBindGroupLayout& resultBindGroupLayout, const WGPUSamplerBindingType*& resultSamplerTypes)
{
    resultSamplerTypes = nullptr;
    const GPUComputePipelineKeyWebGPU key = GetComputePipelineKey(float32Filterable, bindings, DescriptorInfo);
    auto found = _pipelines.Find(key);
    if (found.IsNotEnd())
    {
        resultBindGroupLayout = found->Value.BindGroupLayout;
        resultSamplerTypes = found->Value.SamplerTypes;
        return found->Value.Pipeline;
    }
    PROFILE_CPU();
    ZoneText(*_name, _name.Length());
#if WEBGPU_LOG_PSO
#ifdef WEBGPU_LOG_PSO_NAME
    const bool log = _name.Contains(WEBGPU_LOG_PSO_NAME);
#else
    const bool log = true;
#endif
    if (log)
        LOG(Info, "[WebGPU] GetPipeline: '{}'", String(_name));
#else
    const bool log = false;
#endif

    // Create layout bind group
    BindGroupEntries entries;
    entries.Resize(DescriptorInfo.DescriptorTypesCount);
    GPUComputePipelineWebGPU cacheEntry;
    cacheEntry.BindGroupLayout = CreateBindGroupLayout(device, float32Filterable, bindings, 0, DescriptorInfo, entries, _name, log, true, cacheEntry.SamplerTypes);
    if (!cacheEntry.BindGroupLayout)
    {
        WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        cacheEntry.BindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
    }
    resultBindGroupLayout = cacheEntry.BindGroupLayout;

    // Create the pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
#if GPU_ENABLE_RESOURCE_NAMING
    layoutDesc.label = { _name.Get(), (size_t)_name.Length() };
#endif
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &cacheEntry.BindGroupLayout;
    auto layout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
    if (!layout)
    {
        LOG(Error, "wgpuDeviceCreatePipelineLayout failed");
        if (cacheEntry.BindGroupLayout)
            wgpuBindGroupLayoutRelease(cacheEntry.BindGroupLayout);
        resultBindGroupLayout = nullptr;
        return nullptr;
    }

    // Create pipeline
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
#if GPU_ENABLE_RESOURCE_NAMING
    desc.label = layoutDesc.label;
#endif
    desc.layout = layout;
    desc.compute.module = ShaderModule;
    cacheEntry.Pipeline = wgpuDeviceCreateComputePipeline(device, &desc);
    wgpuPipelineLayoutRelease(layout);
    if (!cacheEntry.Pipeline)
    {
#if GPU_ENABLE_RESOURCE_NAMING
        LOG(Error, "wgpuDeviceCreateComputePipeline failed for {}", String(_name));
#endif
        if (cacheEntry.BindGroupLayout)
            wgpuBindGroupLayoutRelease(cacheEntry.BindGroupLayout);
        resultBindGroupLayout = nullptr;
        return nullptr;
    }

    _pipelines.Add(key, cacheEntry);
    found = _pipelines.Find(key);
    resultSamplerTypes = found.IsNotEnd() ? found->Value.SamplerTypes : nullptr;
    return cacheEntry.Pipeline;
}

void GPUPipelineStateWebGPU::OnReleaseGPU()
{
    VS = nullptr;
    PS = nullptr;
#if WEBGPU_CACHE_BIND_GROUPS
    for (auto& e : _bindGroups)
        wgpuBindGroupRelease(e.Value);
    _bindGroups.Clear();
#endif
    for (auto& e : _pipelines)
        wgpuRenderPipelineRelease(e.Value);
    _pipelines.Clear();
    for (auto& e : BindGroupLayouts)
    {
        if (e)
        {
            wgpuBindGroupLayoutRelease(e);
            e = nullptr;
        }
    }
    if (PipelineDesc.layout)
    {
        wgpuPipelineLayoutRelease(PipelineDesc.layout);
        PipelineDesc.layout = nullptr;
    }
    Platform::MemoryClear(&BindGroupDescriptors, sizeof(BindGroupDescriptors));
    Platform::MemoryClear(&BindGroupSamplerTypes, sizeof(BindGroupSamplerTypes));
}

uint32 GetHash(const GPUPipelineStateWebGPU::PipelineKey& key)
{
    static_assert(sizeof(GPUPipelineStateWebGPU::PipelineKey) == sizeof(uint64) * 2, "Invalid PSO key size.");
    uint32 hash = GetHash(key.Packed[0]);
    CombineHash(hash, GetHash(key.Packed[1]));
    return hash;
}

uint32 GetHash(const GPUBindGroupKeyWebGPU& key)
{
    return key.Hash;
}

uint32 GetHash(const GPUComputePipelineKeyWebGPU& key)
{
    return key.Hash;
}

bool GPUBindGroupKeyWebGPU::operator==(const GPUBindGroupKeyWebGPU& other) const
{
    return Hash == other.Hash
        && Layout == other.Layout
        && EntriesCount == other.EntriesCount
        && Platform::MemoryCompare(&Entries, &other.Entries, EntriesCount * sizeof(WGPUBindGroupEntry)) == 0
        && Platform::MemoryCompare(&Versions, &other.Versions, EntriesCount * sizeof(uint32)) == 0;
}

bool GPUComputePipelineKeyWebGPU::operator==(const GPUComputePipelineKeyWebGPU& other) const
{
    return Hash == other.Hash
        && SampleTypesCount == other.SampleTypesCount
        && Platform::MemoryCompare(SampleTypes, other.SampleTypes, SampleTypesCount * sizeof(uint8)) == 0;
}

WGPUBindGroup GPUBindGroupCacheWebGPU::Get(WGPUDevice device, GPUBindGroupKeyWebGPU& key, const StringAnsiView& debugName, uint64 gcFrames)
{
#if WEBGPU_LOG_BIND_GROUPS
#ifdef WEBGPU_LOG_PSO_NAME
    const bool log = debugName.Contains(WEBGPU_LOG_PSO_NAME);
#else
    const bool log = true;
#endif
#endif

    // Generate a hash for the key
    key.LastFrameUsed = Engine::FrameCount;
    key.Hash = Crc::MemCrc32(&key.Entries, key.EntriesCount * sizeof(WGPUBindGroupEntry));
    CombineHash(key.Hash, GetHash(key.EntriesCount));
    CombineHash(key.Hash, GetHash(key.Layout));
    CombineHash(key.Hash, Crc::MemCrc32(&key.Versions, key.EntriesCount * sizeof(uint8)));

    // Lookup for existing bind group
    WGPUBindGroup bindGroup;
#if WEBGPU_CACHE_BIND_GROUPS
    auto found = _bindGroups.Find(key);
    if (found.IsNotEnd())
    {
        // Get cached bind group and update the last usage frame
        bindGroup = found->Value;
        found->Key.LastFrameUsed = key.LastFrameUsed;

        // Periodically remove old bind groups (unused for some time)
        if (key.LastFrameUsed - _lastFrameBindGroupsGC > gcFrames * 2)
        {
            _lastFrameBindGroupsGC = key.LastFrameUsed;
            int32 freed = 0;
            for (auto it = _bindGroups.Begin(); it.IsNotEnd(); ++it)
            {
                if (key.LastFrameUsed - it->Key.LastFrameUsed > gcFrames)
                {
                    freed++;
                    wgpuBindGroupRelease(it->Value);
                    _bindGroups.Remove(it);
                }
            }
#if WEBGPU_LOG_BIND_GROUPS
            if (freed > 0 && log)
                LOG(Info, "[WebGPU] Removed {} old entries from '{}'", freed, String(debugName));
#endif
        }

        return bindGroup;
    }
#endif
    PROFILE_CPU();
    PROFILE_MEM(GraphicsShaders);
#if GPU_ENABLE_RESOURCE_NAMING
    ZoneText(debugName.Get(), debugName.Length());
#endif
#if WEBGPU_LOG_BIND_GROUPS
    if (log)
        LOG(Info, "[WebGPU] GetBindGroup: '{}', hash: {}", String(debugName), key.Hash);
#endif

    // Build description
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
#if GPU_ENABLE_RESOURCE_NAMING
    desc.label = { debugName.Get(), (size_t)debugName.Length() };
#endif
    desc.layout = key.Layout;
    desc.entryCount = key.EntriesCount;
    desc.entries = key.Entries;

    // Create object
    bindGroup = wgpuDeviceCreateBindGroup(device, &desc);
    if (!bindGroup)
    {
#if GPU_ENABLE_RESOURCE_NAMING
        LOG(Error, "wgpuDeviceCreateBindGroup failed for {}", String(debugName));
#endif
        return nullptr;
    }

#if WEBGPU_LOG_BIND_GROUPS
    // Debug detection of hash collisions
    int32 collisions = 0, equalLayout = 0, equalEntries = 0, equalVersions = 0;
    for (auto& e : _bindGroups)
    {
        auto& other = e.Key;
        if (key.Hash == other.Hash)
        {
            collisions++;
            if (key.Layout == other.Layout)
                equalLayout++;
            if (key.EntriesCount == other.EntriesCount && Platform::MemoryCompare(&key.Entries, &other.Entries, key.EntriesCount * sizeof(WGPUBindGroupEntry)) == 0)
                equalEntries++;
            if (key.EntriesCount == other.EntriesCount && Platform::MemoryCompare(&key.Versions, &other.Versions, key.EntriesCount * sizeof(uint32)) == 0)
                equalVersions++;
        }
    }
    if (collisions > 1 && log)
        LOG(Error, "> Hash collision! {}/{} (capacity: {}), equalLayout: {}, equalEntries: {}, equalVersions: {}", collisions, _bindGroups.Count(), _bindGroups.Capacity(), equalLayout, equalEntries, equalVersions);
#endif

#if WEBGPU_CACHE_BIND_GROUPS
    // Cache it
    _bindGroups.Add(key, bindGroup);
#endif
    return bindGroup;
}

WGPURenderPipeline GPUPipelineStateWebGPU::GetPipeline(const PipelineKey& key, const GPUContextBindingsWebGPU& bindings)
{
    WGPURenderPipeline pipeline;
    if (_pipelines.TryGet(key, pipeline))
        return pipeline;
    PROFILE_CPU();
    PROFILE_MEM(GraphicsShaders);
#if GPU_ENABLE_RESOURCE_NAMING
    ZoneText(_debugName.Get(), _debugName.Count() - 1);
#endif
#if WEBGPU_LOG_PSO
#ifdef WEBGPU_LOG_PSO_NAME
    const bool log = StringAnsiView(_debugName.Get(), _debugName.Count() - 1).Contains(WEBGPU_LOG_PSO_NAME);
#else
    const bool log = true;
#endif
    if (log)
        LOG(Info, "[WebGPU] GetPipeline: '{}'", String(_debugName.Get(), _debugName.Count() - 1));
#endif

    // Lazy-init layout (cannot do it during Init as texture samplers that access eg. depth need to explicitly use UnfilterableFloat)
    if (!PipelineDesc.layout)
        InitLayout(bindings);

    // Build final pipeline description
    auto desc = PipelineDesc;
    WGPUDepthStencilState depthStencilDesc = _depthStencilDesc;
    depthStencilDesc.format = (WGPUTextureFormat)key.DepthStencilFormat;
    if (!HasStencilFormat(depthStencilDesc.format))
    {
        WGPUStencilFaceState stencilFace = WGPU_STENCIL_FACE_STATE_INIT;
        depthStencilDesc.stencilFront = stencilFace;
        depthStencilDesc.stencilBack = stencilFace;
        depthStencilDesc.stencilReadMask = 0;
        depthStencilDesc.stencilWriteMask = 0;
    }
    desc.depthStencil = key.DepthStencilFormat ? &depthStencilDesc : nullptr; // Unbind depth stencil state when no debug buffer is bound
    desc.multisample.count = key.MultiSampleCount;
    if (PS)
    {
        _fragmentDesc.targetCount = key.RenderTargetCount;
        for (int32 i = 0; i < _fragmentDesc.targetCount; i++)
            _colorTargets[i].format = (WGPUTextureFormat)key.RenderTargetFormats[i];
    }
    WGPUVertexBufferLayout buffers[GPU_MAX_VB_BINDED];
    WGPUVertexAttribute attributes[GPU_MAX_VS_ELEMENTS];
    if (key.VertexLayout)
    {
        // Combine input layout of Vertex Buffers with the destination layout used by the Vertex Shader
        GPUVertexLayoutWebGPU* mergedVertexLayout = key.VertexLayout;
        if (!mergedVertexLayout)
            mergedVertexLayout = (GPUVertexLayoutWebGPU*)VS->Layout; // Fallback to shader-specified layout (if using old APIs)
        if (VS->InputLayout)
            mergedVertexLayout = (GPUVertexLayoutWebGPU*)GPUVertexLayout::Merge(mergedVertexLayout, VS->InputLayout, true, true, -1, true);

        // Build attributes list
        desc.vertex.bufferCount = 0;
        desc.vertex.buffers = buffers;
        int32 attributeIndex = 0;
        auto& elements = mergedVertexLayout->GetElements();
#if WEBGPU_LOG_PSO
        if (log)
            LOG(Info, " > Vertex elements: {}", elements.Count());
#endif
        for (int32 bufferIndex = 0; bufferIndex < GPU_MAX_VB_BINDED; bufferIndex++)
        {
            auto& buffer = buffers[bufferIndex];
            buffer.nextInChain = nullptr;
            buffer.stepMode = WGPUVertexStepMode_Vertex;
            buffer.arrayStride = 0;
            buffer.attributeCount = 0;
            buffer.attributes = attributes + attributeIndex;
            for (int32 i = 0; i < elements.Count(); i++)
            {
                const VertexElement& element = elements[i];
                if (element.Slot != bufferIndex)
                    continue;
                WGPUVertexAttribute& dst = attributes[attributeIndex++];
                buffer.attributeCount++;
                dst.nextInChain = nullptr;
                dst.format = RenderToolsWebGPU::ToVertexFormat(element.Format);
                dst.offset = element.Offset;
                dst.shaderLocation = i; // Elements are sorted to match Input Layout order of Vertex Shader as provided by GPUVertexLayout::Merge
                if (element.PerInstance)
                    buffer.stepMode = WGPUVertexStepMode_Instance;
                buffer.arrayStride = Math::Max<uint64>(buffer.arrayStride, element.Offset + PixelFormatExtensions::SizeInBytes(element.Format));
                desc.vertex.bufferCount = Math::Max<size_t>(desc.vertex.bufferCount, bufferIndex + 1);
#if WEBGPU_LOG_PSO
                if (log)
                    LOG(Info, "   > [{}] slot {}: {} ({} bytes at offset {}) at shader location: {} (per-instance: {})", attributeIndex - 1, element.Slot, ScriptingEnum::ToString(element.Format), PixelFormatExtensions::SizeInBytes(element.Format), element.Offset, dst.shaderLocation, element.PerInstance);
#endif
            }
        }
    }
    else
    {
        // No vertex input
        desc.vertex.bufferCount = 0;
        desc.vertex.buffers = nullptr;
    }

    // Create object
    pipeline = wgpuDeviceCreateRenderPipeline(_device->Device, &desc);
    if (!pipeline)
    {
#if GPU_ENABLE_RESOURCE_NAMING
        LOG(Error, "wgpuDeviceCreateRenderPipeline failed for {}", String(_debugName.Get(), _debugName.Count() - 1));
#endif
        return nullptr;
    }

    // Cache it
    _pipelines.Add(key, pipeline);
    return pipeline;
}

void GPUPipelineStateWebGPU::InitLayout(const GPUContextBindingsWebGPU& bindings)
{
#if GPU_ENABLE_RESOURCE_NAMING
    StringAnsiView debugName(_debugName.Get(), _debugName.Count() - 1);
#else
    StringAnsiView debugName;
#endif
#if WEBGPU_LOG_PSO
#ifdef WEBGPU_LOG_PSO_NAME
    const bool log = debugName.Contains(WEBGPU_LOG_PSO_NAME);
#else
    const bool log = true;
#endif
#else
    const bool log = false;
#endif

    // Count the biggest bind group entries (for all shaders) to allocate reused memory
    int32 maxEntriesCount = 0;
    for (int32 groupIndex = 0; groupIndex < ARRAY_COUNT(BindGroupDescriptors); groupIndex++)
    {
        auto descriptors = BindGroupDescriptors[groupIndex];
        if (descriptors && maxEntriesCount < descriptors->DescriptorTypesCount)
            maxEntriesCount = (int32)descriptors->DescriptorTypesCount;
    }
    BindGroupEntries entries;
    entries.Resize(maxEntriesCount);

    // Setup bind groups
    for (int32 groupIndex = 0; groupIndex < ARRAY_COUNT(BindGroupDescriptors); groupIndex++)
    {
        auto descriptors = BindGroupDescriptors[groupIndex];
        WGPUBindGroupLayout bindGroupLayout = nullptr;
        if (descriptors)
            bindGroupLayout = CreateBindGroupLayout(_device->Device, _device->Float32Filterable, bindings, groupIndex, *descriptors, entries, debugName, log, false, BindGroupSamplerTypes[groupIndex]);
        if (!bindGroupLayout)
        {
            // Firefox and Safari have a bug that causes pipeline creation to fail when bind group layout is empty (no entries) and used in the pipeline layout
            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bindGroupLayout = wgpuDeviceCreateBindGroupLayout(_device->Device, &bindGroupLayoutDesc);
        }
        BindGroupLayouts[groupIndex] = bindGroupLayout;
    }

    // Create the pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
#if GPU_ENABLE_RESOURCE_NAMING
    layoutDesc.label = PipelineDesc.label;
#endif
    layoutDesc.bindGroupLayoutCount = ARRAY_COUNT(BindGroupLayouts);
    layoutDesc.bindGroupLayouts = BindGroupLayouts;
    PipelineDesc.layout = wgpuDeviceCreatePipelineLayout(_device->Device, &layoutDesc);
    if (!PipelineDesc.layout)
    {
        LOG(Error, "wgpuDeviceCreatePipelineLayout failed");
    }
}

bool GPUPipelineStateWebGPU::IsValid() const
{
    return _memoryUsage != 0;
}

bool GPUPipelineStateWebGPU::Init(const Description& desc)
{
    if (IsValid())
        OnReleaseGPU();

    // Initialize description (without dynamic state from context such as render targets, vertex buffers, etc.)
    PipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
#if GPU_ENABLE_RESOURCE_NAMING
    DebugDesc = desc;
    GetDebugName(_debugName);
    PipelineDesc.label = { _debugName.Get(), (size_t)_debugName.Count() - 1 };
#endif
    PipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    PipelineDesc.primitive.frontFace = WGPUFrontFace_CW;
    switch (desc.CullMode)
    {
    case CullMode::Normal:
        PipelineDesc.primitive.cullMode = WGPUCullMode_Back;
        break;
    case CullMode::Inverted:
        PipelineDesc.primitive.cullMode = WGPUCullMode_Front;
        break;
    case CullMode::TwoSided:
        PipelineDesc.primitive.cullMode = WGPUCullMode_None;
        break;
    }
    PipelineDesc.primitive.unclippedDepth = !desc.DepthClipEnable && _device->Limits.HasDepthClip;
    PipelineDesc.depthStencil = &_depthStencilDesc;
    _depthStencilDesc = WGPU_DEPTH_STENCIL_STATE_INIT;
    _depthStencilDesc.depthWriteEnabled = desc.DepthEnable && desc.DepthWriteEnable ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    _depthStencilDesc.depthCompare = ToCompareFunction(desc.DepthFunc);
    if (desc.StencilEnable)
    {
        _depthStencilDesc.stencilFront.compare = ToCompareFunction(desc.StencilFunc);
        _depthStencilDesc.stencilFront.failOp = ToStencilOperation(desc.StencilFailOp);
        _depthStencilDesc.stencilFront.depthFailOp = ToStencilOperation(desc.StencilDepthFailOp);
        _depthStencilDesc.stencilFront.passOp = ToStencilOperation(desc.StencilPassOp);
        _depthStencilDesc.stencilBack = _depthStencilDesc.stencilFront;
        _depthStencilDesc.stencilReadMask = desc.StencilReadMask;
        _depthStencilDesc.stencilWriteMask = desc.StencilWriteMask;
    }
    PipelineDesc.multisample.alphaToCoverageEnabled = desc.BlendMode.AlphaToCoverageEnable;
    if (desc.PS)
    {
        PipelineDesc.fragment = &_fragmentDesc;
        _fragmentDesc = WGPU_FRAGMENT_STATE_INIT;
        _fragmentDesc.targets = _colorTargets;
        Platform::MemoryClear(&_colorTargets, sizeof(_colorTargets));
        if (desc.BlendMode.BlendEnable)
        {
            _blendState = WGPU_BLEND_STATE_INIT;
            _blendState.color = ToBlendComponent(desc.BlendMode.BlendOp, desc.BlendMode.SrcBlend, desc.BlendMode.DestBlend);
            _blendState.alpha = ToBlendComponent(desc.BlendMode.BlendOpAlpha, desc.BlendMode.SrcBlendAlpha, desc.BlendMode.DestBlendAlpha);
            for (auto& e : _colorTargets)
                e.blend = &_blendState;
        }
        WGPUColorWriteMask writeMask = WGPUColorWriteMask_All;
        if (desc.BlendMode.RenderTargetWriteMask != BlendingMode::ColorWrite::All)
        {
            writeMask = 0;
            if (EnumHasAllFlags(desc.BlendMode.RenderTargetWriteMask, BlendingMode::ColorWrite::Red))
                writeMask |= WGPUColorWriteMask_Red;
            if (EnumHasAllFlags(desc.BlendMode.RenderTargetWriteMask, BlendingMode::ColorWrite::Green))
                writeMask |= WGPUColorWriteMask_Green;
            if (EnumHasAllFlags(desc.BlendMode.RenderTargetWriteMask, BlendingMode::ColorWrite::Blue))
                writeMask |= WGPUColorWriteMask_Blue;
            if (EnumHasAllFlags(desc.BlendMode.RenderTargetWriteMask, BlendingMode::ColorWrite::Alpha))
                writeMask |= WGPUColorWriteMask_Alpha;
        }
        uint16 outputsCount = desc.PS->GetBindings().OutputsCount;
        for (uint16 rtIndex = 0; rtIndex < outputsCount; rtIndex++)
            _colorTargets[rtIndex].writeMask = writeMask;
    }

    // Cache shaders
    VS = (GPUShaderProgramVSWebGPU*)desc.VS;
    BindGroupDescriptors[GPUBindGroupsWebGPU::Vertex] = &VS->DescriptorInfo;
    PipelineDesc.vertex.module = VS->ShaderModule;
    PS = (GPUShaderProgramPSWebGPU*)desc.PS;
    if (PS)
    {
        BindGroupDescriptors[GPUBindGroupsWebGPU::Pixel] = &PS->DescriptorInfo;
        _fragmentDesc.module = PS->ShaderModule;
    }

    _memoryUsage = 1;
    return GPUPipelineState::Init(desc);
}

#endif
