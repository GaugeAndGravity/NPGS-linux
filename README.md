# NPGS

[NPGS](https://github.com/baopinshui/NPGS) 是使用 Vulkan 与 GLSL 的天体模拟和黑洞可视化项目。本分支面向 Linux 原生 Wayland 桌面，使用 CMake + Ninja 构建。

## 核心能力

- 克尔–纽曼时空光线追踪：旋转带电黑洞、内外视界、能层、奇环与最大延拓。
- 吸积盘、相对论喷流、引力透镜、多普勒频移、引力红移与热浪折射。
- 基于 Walker–Penrose 常数的偏振态演化。
- 绕转、自由与测地线相机，以及可交互的参数界面。

## 环境

- Linux、Wayland 会话、支持 Wayland 的 GLFW ≥ 3.4。
- Vulkan ≥ 1.4；同一队列族提供 graphics、compute 与 present。
- SDR surface：`R8G8B8A8_UNORM` 或 `B8G8R8A8_UNORM`，搭配 `SRGB_NONLINEAR`。
- CMake ≥ 3.28、Ninja、C++23 编译器与标准库、Vulkan 着色器工具。
- Fontconfig 与覆盖所用语言的系统字体，中文推荐 Noto CJK。

完整依赖及 Gentoo 安装命令见[构建与安装](doc/build.md)。

## 快速开始

在仓库根目录执行：

```sh
cmake --preset linux-release
cmake --build --preset linux-release
./build/release/bin/npgs
```

程序默认窗口请求为 1280×960。在主菜单选择“新的模拟”→“开始模拟”进入界面；可用 `T` 切换绕转与自由相机，按 `Esc` 退出。

吸积盘切换与显示参数见[使用指南](doc/usage.md#吸积盘与参数)。

## 文档

- [构建与安装](doc/build.md)：Gentoo 依赖、CMake presets、用户目录安装。
- [使用指南](doc/usage.md)：启动参数、菜单、键鼠操作、字体和资源路径。
- [资源与许可证](doc/licensing.md)：代码许可证、第三方资产与打包要求。
- [开发与测试](doc-dev/development.md)：源码结构、着色器构建、资源布局和测试维护。

## 许可证

代码许可证见 [GNU GPL v3](LICENSE)。第三方资产适用各自许可，分发前请查阅[资源与许可证](doc/licensing.md)。
