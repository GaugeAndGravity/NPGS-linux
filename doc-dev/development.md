# 开发与测试

[返回首页](../README.md) · [用户构建指南](../doc/build.md)

## 源码结构

| 路径 | 职责 |
| --- | --- |
| `NPGS/Sources/Program/` | CLI、应用生命周期、主渲染流程与 Uniform 数据结构 |
| `NPGS/Sources/Engine/Core/Runtime/` | 资源加载、Vulkan 对象、管线与线程运行时 |
| `NPGS/Sources/Engine/Core/System/` | 天体生成、相机、UI 与翻译 |
| `NPGS/Sources/Engine/Shaders/` | GLSL stage、共享物理代码与 include |
| `NPGS/Assets/` | UI、翻译、纹理和数据表 |
| `cmake/` | 依赖检测、资源部署、着色器规则与检查 |
| `tests/` | CPU 回归、资源 / SPIR-V / CLI 检查和 Wayland 测试 |

应用由 GLFW 创建原生 Wayland 窗口，使用 Vulkan 1.4 渲染。主管线采用 KN 预通道、合成、Bloom 与混合；共享 offscreen 资源通过前帧 fence 等待串行复用。

UI 采用同版 Dear ImGui docking core 与官方 GLFW / Vulkan backend。Fontconfig 匹配 `sans-serif regular` 主字体，并从排序结果选择补齐中文与 UI 符号的 fallback 字体；字体源通过官方 `MergeMode` 合并，字形与纹理使用官方动态接口，显示比例来自 framebuffer scale。

自动匹配保留 TTC face index；`NPGS_FONT` 绝对文件覆盖主字体，TTC 使用 face 0，系统 fallback 继续参与补字。用户接口见[字体说明](../doc/usage.md#字体)。

## 日常构建

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

`build/debug/compile_commands.json` 可用于 clangd 等工具。Release preset 为 `linux-release`，产物位于 `build/release/`。

常用配置：

| 变量 | 默认值 | 用途 |
| --- | --- | --- |
| `NPGS_BUILD_APPLICATION` | `ON` | 构建应用；设为 `OFF` 可进行资源与着色器构建 |
| `NPGS_FETCH_MISSING_DEPENDENCIES` | `ON` | 获取缺失的固定版本源码依赖 |
| `NPGS_USE_PCH` | `ON` | 使用预编译头 |
| `BUILD_TESTING` | `ON` | 配置 CTest |
| `NPGS_TEST_WAYLAND` | `OFF` | 将真实窗口与缩放测试加入 CTest |

依赖版本与源码摘要由 [`NpgsDependencies.cmake`](../cmake/NpgsDependencies.cmake) 管理。升级 Dear ImGui 时，应同步 core 与两个 backend，并维护动态字体协议回归。

## GLSL 与 SPIR-V

[`NpgsResources.cmake`](../cmake/NpgsResources.cmake) 将 GLSL 编译纳入 Ninja 增量构建，目标环境为 Vulkan 1.4，include 依赖通过 depfile 追踪。宏变体与输出名来自 [`CompileShaders.cfg`](../NPGS/Tools/ShaderCompiler/CompileShaders.cfg)。

当前 KN 主管线使用：

- `ScreenQuad.vert.spv`
- `BlackHole_prepass.frag.spv`
- `BlackHole_composite.frag.spv`
- `PreBloom.comp.spv`
- `GaussBlur.comp.spv`
- `ColorBlend.frag.spv`

`PreBloom` 与 `GaussBlur` 是 `Bloom.comp.glsl` 的宏变体。共享 `.glsl` 作为 include 依赖；stage 构建范围由 CMake 的 stage 列表和过滤规则决定。

单独构建着色器，或使用资源 preset：

```sh
cmake --build build/debug --target npgs_shaders
cmake --preset linux-resources
cmake --build --preset linux-resources
ctest --preset linux-resources
```

修改 Uniform 时，同步更新 [`DataStructures.h`](../NPGS/Sources/Program/DataStructures.h)、GLSL block 与相关静态断言；修改 descriptor 或宏变体时，核对优化后 SPIR-V 反射信息。

## 资源布局

`npgs_resources` 部署运行资源，`npgs_shaders` 生成着色器；应用依赖这两个目标。构建树与安装树采用相同布局：

```text
bin/npgs
share/npgs/Assets/
```

生成的 `npgs-resource-manifest.txt` 以 `share/npgs/` 为根记录文件。新增资源时，保持构建、manifest、安装规则与 [`CheckResources.cmake`](../cmake/CheckResources.cmake) 一致，并核对[资源授权](../doc/licensing.md)。

运行资源按可执行文件位置发现，`NPGS_ASSET_DIR` 作为显式绝对路径覆盖。可写的导出与缓存使用 XDG 目录，详见[使用指南](../doc/usage.md#资源与用户文件)。

## 测试维护

默认 CTest 运行 CPU 与静态检查：

| 测试 | 关注点 |
| --- | --- |
| `npgs-scroll` | 滚轮百分比设置、分数输入、正反向倍率、禁用调整与相机模式 |
| `npgs-imgui-fonts` | 字体选择、动态字形、字号 / framebuffer scale、纹理协议与容量检查 |
| `npgs-imgui-cjk` | 系统 CJK 字体覆盖；缺少相应字体时报告跳过 |
| `npgs-resource-layout` | manifest、必需文件与资源布局 |
| `npgs-spirv` | Vulkan 1.4 SPIR-V 校验与主管线产物，需要 `spirv-val` |
| `npgs-acceptance-selftest` | 测试辅助脚本的进程、超时与结果判定 |
| `npgs-cli` | 帮助、参数组合和错误退出码 |

`linux-resources` preset 运行资源与着色器相关检查。默认 CPU 检查适用于无图形会话的构建主机。

测试脚本的持久日志默认保存在仓库的 `local/acceptance/`。临时 fixture 使用 Python `tempfile`，可通过 `TMPDIR` 选择临时目录。

### 图形测试

`NPGS_TEST_WAYLAND=ON` 会启用实际创建窗口、执行渲染与缩放的 `npgs-wayland` 测试，需要可用的 Wayland 会话和 Vulkan 设备。

有限帧运行以成功 present 计数；缩放测试需要实际 framebuffer 变化、swapchain 重建及重建后的 present。测试尺寸由 compositor 最终决定。人工检查应覆盖画面、中文 UI、相机、键鼠、缩放、关闭和截图。

## 辅助生成工具

| 工具 | 用途 |
| --- | --- |
| [`TieAsTupleGenerator.py`](../NPGS/Tools/CodeGenerator/TieAsTupleGenerator.py) | 生成结构化绑定与 tuple 转换的 C++ 重载 |
| [`H-R_Diagram_Fitting.py`](../NPGS/Tools/H-RDiagram/H-R_Diagram_Fitting.py) | 从配套 H–R 图提取曲线并生成 CSV，需要 NumPy 与 OpenCV |

两个工具默认输出到仓库的 `local/generated/`，可用 `--output` 指定输出位置。

## 文档维护

用户文档放在 `doc/`，架构和维护方法放在 `doc-dev/`。本机开发记录、诊断证据与运行日志放在 Git 忽略的 `local/`。
