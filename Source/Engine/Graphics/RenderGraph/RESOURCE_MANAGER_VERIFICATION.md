# RenderGraphResourceManager 验证指南

本文档说明如何验证 RenderGraphResourceManager 的功能是否正确实现。

## 实现的功能

### 1. 资源生命周期管理
- **资源分配**：`AllocateTexture()` 和 `AllocateBuffer()` 从池中分配或创建新资源
- **资源释放**：`ReleaseTexture()` 和 `ReleaseBuffer()` 将资源标记为可复用
- **自动清理**：`ReleaseUnusedResources()` 清理长时间未使用的资源

### 2. 资源池化
- 维护纹理池 (`_texturePool`) 和缓冲区池 (`_bufferPool`)
- 通过描述哈希快速匹配可复用资源
- 从池尾向前搜索以提高缓存局部性和复用率

### 3. 资源别名分配
- `SetupAliasing()` 初始化别名信息
- `SetTextureAliasing()` 和 `SetBufferAliasing()` 设置资源别名
- 支持编译器优化的内存别名以减少内存占用

### 4. 内存优化
- 跟踪总内存使用量 (`_textureMemoryAllocated`, `_bufferMemoryAllocated`)
- 统计每帧的分配和复用次数
- 提供详细的内存使用报告

## 验证步骤

### 步骤 1：编译验证
确保代码可以成功编译：
```bash
# 编译 FlaxEngine 项目
# 检查是否有编译错误
```

**预期结果**：无编译错误，RenderGraphResourceManager 正确集成到 RenderGraph 系统中。

### 步骤 2：资源分配验证
创建一个简单的 RenderGraph 并分配资源：

```cpp
RenderGraph graph;

// 创建纹理资源
auto texDesc = RenderGraphTextureDesc::Create2D(1920, 1080, PixelFormat::R8G8B8A8_UNorm);
auto texRef = graph.CreateTexture(texDesc);

// 编译图
graph.Compile();

// 验证资源已分配
GPUTexture* texture = graph.GetTexture(texRef);
ASSERT(texture != nullptr);
```

**预期结果**：
- 资源成功分配
- `GetTexturesAllocatedThisFrame()` 返回 1
- `GetTextureMemoryAllocated()` 返回正确的内存大小

### 步骤 3：资源复用验证
在多帧中使用相同的资源描述：

```cpp
// 第一帧
RenderGraph graph1;
auto tex1 = graph1.CreateTexture(texDesc);
graph1.Compile();
graph1.Execute(context);
graph1.Clear(); // 释放资源回池

// 第二帧 - 使用相同描述
RenderGraph graph2;
auto tex2 = graph2.CreateTexture(texDesc);
graph2.Compile();

// 验证资源被复用
auto* manager = graph2.GetResourceManager();
ASSERT(manager->GetTexturesReusedThisFrame() == 1);
ASSERT(manager->GetTexturesAllocatedThisFrame() == 0);
```

**预期结果**：
- 第二帧复用了第一帧的纹理
- 没有创建新的纹理
- 内存使用量保持不变

### 步骤 4：资源释放验证
验证长时间未使用的资源被正确释放：

```cpp
auto* manager = graph.GetResourceManager();

// 记录初始状态
int32 initialPoolSize = manager->GetTexturePoolSize();
uint64 initialMemory = manager->GetTextureMemoryAllocated();

// 等待足够的帧数（默认 180 帧）
// 然后调用清理
manager->ReleaseUnusedResources(false, 0);

// 验证资源被释放
ASSERT(manager->GetTexturePoolSize() < initialPoolSize);
ASSERT(manager->GetTextureMemoryAllocated() < initialMemory);
```

**预期结果**：
- 未使用的资源被释放
- 池大小减少
- 内存使用量减少

### 步骤 5：资源别名验证
验证编译器可以设置资源别名：

```cpp
auto* manager = graph.GetResourceManager();
auto* compiler = graph.GetCompiler();

// 编译后设置别名
graph.Compile();
manager->SetupAliasing(graph.GetTextureCount(), graph.GetBufferCount());
manager->SetTextureAliasing(1, 0); // 资源 1 别名到资源 0

// 验证别名设置
ASSERT(manager->GetTextureAliasing(1) == 0);
```

**预期结果**：
- 别名信息正确设置
- 可以查询别名关系

### 步骤 6：内存统计验证
验证内存统计功能：

```cpp
auto* manager = graph.GetResourceManager();

// 分配一些资源
graph.CreateTexture(RenderGraphTextureDesc::Create2D(1920, 1080, PixelFormat::R8G8B8A8_UNorm));
graph.CreateTexture(RenderGraphTextureDesc::Create2D(1280, 720, PixelFormat::R16G16B16A16_Float));
graph.Compile();

// 检查统计
LOG(Info, "Textures allocated: {0}", manager->GetTexturesAllocatedThisFrame());
LOG(Info, "Texture memory: {0} MB", manager->GetTextureMemoryAllocated() / (1024 * 1024));
LOG(Info, "Pool size: {0}", manager->GetTexturePoolSize());
```

**预期结果**：
- 统计数据准确反映资源使用情况
- 内存计算正确

### 步骤 7：性能分析验证
使用 Profiler 验证性能标记：

```cpp
// 在 Profiler 中查看以下标记：
// - AllocateTexture
// - FindTexture
// - ReuseTexture / CreateNewTexture
// - AllocateBuffer
// - FindBuffer
// - ReuseBuffer / CreateNewBuffer
// - ReleaseUnusedResources
```

**预期结果**：
- 所有关键路径都有性能标记
- 可以在 Profiler 中看到资源分配和复用的时间

## 集成测试

### 完整渲染管线测试
在实际渲染场景中验证：

```cpp
// 创建完整的渲染图
RenderGraph graph;

// 添加多个 Pass
graph.AddPass(new GBufferPass());
graph.AddPass(new LightPass());
graph.AddPass(new PostProcessPass());

// 编译和执行
graph.Compile();
graph.Execute(context);

// 验证资源管理
auto* manager = graph.GetResourceManager();
LOG(Info, "Total textures: {0}", manager->GetTexturePoolSize());
LOG(Info, "Textures allocated: {0}", manager->GetTexturesAllocatedThisFrame());
LOG(Info, "Textures reused: {0}", manager->GetTexturesReusedThisFrame());
LOG(Info, "Memory used: {0} MB", 
    (manager->GetTextureMemoryAllocated() + manager->GetBufferMemoryAllocated()) / (1024 * 1024));

// 清理
graph.Clear();
```

**预期结果**：
- 所有 Pass 的资源正确分配
- 资源在 Pass 之间正确复用
- 没有内存泄漏
- 性能符合预期

## 验证清单

- [ ] 代码编译通过，无错误
- [ ] 资源可以成功分配（纹理和缓冲区）
- [ ] 资源可以从池中复用
- [ ] 资源可以正确释放回池
- [ ] 长时间未使用的资源被自动清理
- [ ] 资源别名功能正常工作
- [ ] 内存统计准确
- [ ] 性能标记正确添加
- [ ] 与 RenderGraph 正确集成
- [ ] 与 RenderGraphCompiler 正确集成
- [ ] 在实际渲染场景中正常工作
- [ ] 没有内存泄漏

## 已知限制

1. **资源别名**：当前实现提供了别名信息的存储和查询，但实际的内存别名需要 GPU API 支持（如 D3D12 的 placed resources）
2. **哈希冲突**：使用描述哈希进行快速匹配，但会进行完整的描述比较以避免冲突
3. **内存计算**：纹理内存大小是估算值，实际 GPU 内存使用可能因对齐和驱动优化而有所不同

## 性能优化

实现中包含的优化：
- 从池尾向前搜索以提高缓存局部性
- 使用描述哈希快速过滤不匹配的资源
- 细粒度的性能标记用于分析
- 延迟清理未使用的资源（默认 180 帧）
- 统计信息用于监控和调优

## 总结

RenderGraphResourceManager 实现了完整的资源生命周期管理、池化和内存优化功能。通过上述验证步骤，可以确保：
1. 资源可以正确分配
2. 资源可以高效复用
3. 资源可以正确释放
4. 内存使用得到优化
5. 性能符合预期
