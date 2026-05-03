# 子任务 5.1, 5.2, 5.4 实现说明

## 概述

本文档说明了子任务 5.1、5.2 和 5.4 的实现细节，这些子任务将主渲染器集成到 RenderGraph 架构中。

## 子任务 5.1: 创建 RenderGraph 构建器

### 实现位置
- **文件**: `Source/Engine/Renderer/Renderer.h` 和 `Source/Engine/Renderer/Renderer.cpp`
- **函数**: `Renderer::BuildRenderGraph(RenderGraph& graph, RenderContext& renderContext, RenderContextBatch& renderContextBatch)`

### 功能描述
根据场景设置和渲染配置动态构建渲染图，支持以下特性：

1. **条件性 Pass 添加**: 根据 `RenderSetup` 和 `ViewFlags` 动态启用/禁用 Pass
2. **调试模式支持**: 处理各种调试视图模式（GBuffer Debug、Material Complexity 等）
3. **渲染特性开关**: 
   - 全局 SDF (Global SDF)
   - 全局表面图集 (Global Surface Atlas)
   - 运动向量 (Motion Vectors)
   - 环境光遮蔽 (Ambient Occlusion)
   - 阴影 (Shadows)
   - 全局光照 (Global Illumination - DDGI)
   - 反射 (Reflections)
   - 屏幕空间反射 (Screen Space Reflections)
   - 体积雾 (Volumetric Fog)
   - 景深 (Depth of Field)
   - 运动模糊 (Motion Blur)
   - 眼睛适应 (Eye Adaptation)
   - 色彩分级 (Color Grading)
   - 后处理 (Post Processing)
   - 抗锯齿 (TAA, FXAA, SMAA)
   - 对比度自适应锐化 (CAS)

### 实现的 Pass 顺序
```
1. GBuffer Pass (延迟渲染)
2. Motion Vectors Pass (运动向量生成)
3. Ambient Occlusion Pass (环境光遮蔽)
4. Shadows Pass (阴影贴图)
5. Light Pass (延迟光照)
6. Global Illumination Pass (全局光照)
7. Reflections Pass (反射)
8. Screen Space Reflections Pass (屏幕空间反射)
9. Volumetric Fog Pass (体积雾)
10. Forward Pass (前向渲染/透明物体)
11. Depth of Field Pass (景深)
12. Eye Adaptation Pass (眼睛适应)
13. Histogram Pass (直方图)
14. Color Grading Pass (色彩分级)
15. Post Processing Pass (后处理)
16. Temporal Anti-Aliasing Pass (时序抗锯齿)
17. FXAA/SMAA Pass (快速/形态学抗锯齿)
18. Contrast Adaptive Sharpening Pass (对比度自适应锐化)
```

## 子任务 5.2: 重构 Renderer::Render 主函数

### 实现位置
- **文件**: `Source/Engine/Renderer/Renderer.cpp`
- **函数**: `RenderInner(SceneRenderTask* task, RenderContext& renderContext, RenderContextBatch& renderContextBatch)`

### 功能描述
修改主渲染函数，添加 RenderGraph 执行路径，同时保持向后兼容：

1. **双路径架构**: 
   - 新路径：使用 RenderGraph 执行渲染
   - 旧路径：保留原有的硬编码流程
   
2. **运行时切换**: 通过 `useRenderGraph` 标志控制使用哪个路径

3. **RenderGraph 执行流程**:
   ```cpp
   // 创建渲染图
   RenderGraph graph;
   
   // 构建渲染图（添加 Pass）
   BuildRenderGraph(graph, renderContext, renderContextBatch);
   
   // 编译渲染图（优化、解析依赖）
   graph.Compile();
   
   // 执行渲染图
   graph.Execute(context);
   ```

4. **错误处理**: 添加了编译和执行失败的错误日志

5. **性能分析**: 添加了 `PROFILE_GPU_CPU_NAMED` 标记用于性能分析

### 向后兼容性
- 默认情况下 `useRenderGraph = false`，使用遗留渲染路径
- 可以通过修改标志启用 RenderGraph 路径
- 两个路径共享相同的设置和准备逻辑

## 子任务 5.4: 更新 RendererService 初始化

### 实现位置
- **文件**: `Source/Engine/Renderer/Renderer.cpp`
- **函数**: `RendererService::Init()` 和 `RendererService::Dispose()`

### 功能描述
修改渲染器服务的初始化和清理逻辑，适配 RenderGraph 架构：

1. **Pass 生命周期管理**:
   - **旧架构**: Pass 是全局单例，在服务初始化时创建，在服务销毁时释放
   - **新架构**: Pass 由 RenderGraph 创建和管理，每帧创建新实例
   - **兼容性**: 保留单例实例以支持遗留渲染路径和向后兼容

2. **初始化逻辑**:
   ```cpp
   // 注册 Pass（单例实例用于遗留渲染路径）
   // 注意：在 RenderGraph 架构中，Pass 是每帧创建的，由图拥有
   // 这些单例实例保留用于向后兼容和遗留渲染路径
   PassList.Add(GBufferPass::Instance());
   PassList.Add(ShadowsPass::Instance());
   // ... 其他 Pass
   ```

3. **清理逻辑**:
   ```cpp
   // 释放子服务（单例 Pass 用于遗留路径）
   // 注意：在 RenderGraph 架构中，Pass 由图拥有并自动清理
   // 这些单例实例保留用于向后兼容
   for (int32 i = 0; i < PassList.Count(); i++)
   {
       PassList[i]->Dispose();
   }
   ```

4. **架构说明**:
   - 添加了详细的注释说明新旧架构的差异
   - 明确了单例实例的保留原因
   - 说明了 RenderGraph 中 Pass 的生命周期管理

## 验证方法

### 编译验证
1. 编译项目，确保没有语法错误
2. 检查所有头文件包含正确
3. 验证所有引用的类和函数存在

### 功能验证
1. **遗留路径测试**:
   - 保持 `useRenderGraph = false`
   - 运行游戏，验证渲染正常工作
   - 确认所有渲染特性正常（阴影、反射、后处理等）

2. **RenderGraph 路径测试**:
   - 设置 `useRenderGraph = true`
   - 运行游戏，验证渲染正常工作
   - 对比两个路径的视觉输出，确保一致性

3. **性能测试**:
   - 使用 Profiler 分析两个路径的性能
   - 验证 RenderGraph 路径的性能不低于遗留路径
   - 检查内存使用情况

4. **调试模式测试**:
   - 测试各种调试视图模式（GBuffer、深度、法线等）
   - 验证 Pass 剔除正确工作
   - 确认资源管理正确

## 实现特点

### 1. 渐进式迁移
- 支持运行时切换新旧渲染路径
- 保持完全向后兼容
- 允许逐步验证和优化

### 2. 动态图构建
- 根据场景设置动态添加 Pass
- 自动处理依赖关系
- 支持条件性渲染特性

### 3. 错误处理
- 编译失败时记录错误并回退
- 执行失败时记录错误并回退
- 保证渲染稳定性

### 4. 性能优化
- 添加性能分析标记
- 支持 Pass 剔除
- 支持资源复用

## 后续工作

1. **运行时配置**: 添加配置选项以在运行时切换 RenderGraph
2. **性能优化**: 根据性能分析结果优化图构建和执行
3. **特殊 Pass 迁移**: 迁移 GlobalSDF 和 GlobalSurfaceAtlas 等特殊 Pass
4. **自定义 PostFx**: 集成自定义后处理效果到 RenderGraph
5. **编辑器集成**: 添加编辑器工具以可视化和调试 RenderGraph

## 注意事项

1. **Pass 实例化**: 使用 `New<>` 创建 Pass 实例，由 RenderGraph 管理生命周期
2. **资源管理**: Pass 通过 RenderGraphBuilder 声明资源依赖
3. **执行顺序**: 由 RenderGraph 编译器根据依赖关系自动确定
4. **内存管理**: RenderGraph 在 Clear() 或析构时自动释放所有 Pass
5. **线程安全**: 当前实现假设单线程渲染，多线程支持需要额外工作

## 总结

本次实现完成了主渲染器到 RenderGraph 架构的集成，提供了：
- ✅ 动态渲染图构建功能
- ✅ 双路径渲染支持（新/旧）
- ✅ 向后兼容性
- ✅ 渐进式迁移路径
- ✅ 完整的错误处理
- ✅ 性能分析支持

这为后续的优化和扩展奠定了坚实的基础。
