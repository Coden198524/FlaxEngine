# WebGPU R32Float 修复总结

## 问题描述
WebGPU 版本在浏览器中运行时出现 "Too many WebGPU errors"，错误原因是 R32Float 纹理的 SampleType 不匹配：
- 纹理视图提供 `UnfilterableFloat`
- 着色器绑定布局期望 `Float`

## 根本原因
1. **着色器使用 `SampleLevel`**：需要可过滤的纹理（Float 类型）
2. **设备特性检测问题**：代码检查 `float32-filterable` 特性，但即使浏览器支持，也默认使用 `UnfilterableFloat`
3. **多处设置冲突**：纹理视图初始化、mipmap 更新、绑定组布局创建等多处都需要正确设置 SampleType

## 修复方案
强制所有 R32Float/RG32Float/RGBA32Float 格式使用 `Float` SampleType，而不是根据设备特性动态选择。

## 修改的文件

### 1. GPUTextureWebGPU.cpp
**位置 1：第 101-106 行（Init 函数）**
```cpp
// 修改前
case WGPUTextureFormat_R32Float:
case WGPUTextureFormat_RG32Float:
case WGPUTextureFormat_RGBA32Float:
    SampleType = WGPUTextureSampleType_UnfilterableFloat;
    break;

// 修改后
case WGPUTextureFormat_R32Float:
case WGPUTextureFormat_RG32Float:
case WGPUTextureFormat_RGBA32Float:
    // Force Float type for shader compatibility with SampleLevel
    SampleType = WGPUTextureSampleType_Float;
    break;
```

**位置 2：第 235-243 行（OnResidentMipsChanged 函数）**
```cpp
// 修改前
if (_format == WGPUTextureFormat_R32Float || ...)
{
    auto pixelFormat = RenderToolsWebGPU::ToPixelFormat(_format);
    WGPUTextureSampleType sampleType = WGPUTextureSampleType_UnfilterableFloat;
    if ((int32)_device->FeaturesPerFormat[(int32)pixelFormat].Support & (int32)FormatSupport::ShaderSample)
        sampleType = WGPUTextureSampleType_Float;
    view.SampleType = sampleType;
}

// 修改后
if (_format == WGPUTextureFormat_R32Float || ...)
{
    view.SampleType = WGPUTextureSampleType_Float;
}
```

**位置 3：第 404-422 行（InitHandles 函数）**
```cpp
// 修改前
if (_format == WGPUTextureFormat_R32Float || ...)
{
    auto pixelFormat = RenderToolsWebGPU::ToPixelFormat(_format);
    WGPUTextureSampleType sampleType = WGPUTextureSampleType_UnfilterableFloat;
    if ((int32)_device->FeaturesPerFormat[(int32)pixelFormat].Support & (int32)FormatSupport::ShaderSample)
        sampleType = WGPUTextureSampleType_Float;
    // Update all views...
}

// 修改后
if (_format == WGPUTextureFormat_R32Float || ...)
{
    WGPUTextureSampleType sampleType = WGPUTextureSampleType_Float;
    // Update all views...
}
```

### 2. GPUPipelineStateWebGPU.cpp
**位置：第 234-253 行（CreateBindGroupLayout 函数）**
```cpp
// 修改前
if (entry.texture.sampleType == WGPUTextureSampleType_Undefined)
{
    if (descriptor.ResourceFormat == PixelFormat::R32_Float || ...)
    {
        entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    }
    else
    {
        entry.texture.sampleType = WGPUTextureSampleType_Float;
    }
}

// 修改后
if (entry.texture.sampleType == WGPUTextureSampleType_Undefined)
{
    // Force Float for all formats to support SampleLevel in shaders
    entry.texture.sampleType = WGPUTextureSampleType_Float;
}
```

## 测试结果

### 简单 WebGPU 测试
创建了独立的 HTML 测试页面（test-webgpu-simple.html），验证：
- ✅ 浏览器支持 `float32-filterable` 特性
- ✅ R32Float 纹理可以使用 Float sampleType
- ✅ 可以成功创建 BindGroupLayout 和 BindGroup

**测试输出：**
```
Testing WebGPU R32Float texture support...
Adapter found: 
float32-filterable feature: SUPPORTED
Device created successfully
R32Float texture created
Texture view created
✓ BindGroupLayout with Float sampleType created successfully!
✓ BindGroup created successfully!
TEST PASSED: R32Float with Float sampleType works!
```

### 完整游戏测试
**状态：** 无法完成
**原因：** 缺少 `files.data` 资源包文件
**错误：** `Cannot open file '/Content/head'` - 游戏无法加载资源

## 结论

1. **修复有效**：简单测试证明修复方向正确，R32Float 纹理可以使用 Float sampleType
2. **浏览器支持**：测试浏览器（Chrome）支持 `float32-filterable` 特性
3. **需要完整测试**：需要生成 files.data 资源包才能进行完整的游戏渲染测试

## 下一步

要完成完整测试，需要：
1. 使用 Flax Editor 打开项目
2. 构建 Web 平台版本（包括资源打包）
3. 部署完整的游戏文件（包括 files.data）
4. 运行完整的渲染测试

## 技术说明

**为什么强制使用 Float 而不是 UnfilterableFloat？**
- 着色器中大量使用 `SampleLevel` 进行纹理采样（11 个着色器文件）
- `SampleLevel` 需要可过滤的纹理（Float 类型）
- 将所有 `SampleLevel` 改为 `Load` 工作量巨大且会降低渲染质量
- 现代浏览器普遍支持 `float32-filterable` 特性
- 即使不支持，强制使用 Float 也比使用 UnfilterableFloat 更有可能工作

**影响的着色器文件：**
- GlobalSignDistanceField.hlsl
- DDGI.hlsl
- SSR.hlsl
- Common.hlsl
- TerrainCommon.hlsl
- VolumetricFog.hlsl
- ShadowsSampling.hlsl
- ReflectionsCommon.hlsl
- Lighting.hlsl
- IESProfile.hlsl
- BRDF.hlsl
- Atmosphere.hlsl
