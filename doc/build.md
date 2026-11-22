# 构建与安装

[返回首页](../README.md) · [使用指南](usage.md)

## 平台与工具

支持 Linux 原生 Wayland 桌面，使用 GLFW ≥ 3.4、Vulkan ≥ 1.4，以及同时提供 graphics、compute、present 的队列族。支持的 SDR surface 为 `R8G8B8A8_UNORM` / `B8G8R8A8_UNORM` + `SRGB_NONLINEAR`。

构建需要：

| 类别 | 依赖 |
| --- | --- |
| 构建工具 | CMake ≥ 3.28、Ninja、pkg-config、Git |
| 编译器 | 支持 C++23 及项目所用标准库功能的 GCC 或 Clang 工具链 |
| 窗口 | GLFW ≥ 3.4（Wayland 后端）、Wayland、wayland-protocols、libxkbcommon |
| 图形 | Vulkan ≥ 1.4 头文件与 loader、匹配 GPU 的 Vulkan 驱动 |
| 着色器 | 支持 Vulkan 1.4 目标的 `glslc` 或 `glslangValidator` / `glslang` |
| 系统库 | GLM ≥ 1.0、fmt、spdlog、Assimp、nlohmann/json ≥ 3.11、Boost.Multiprecision、stb、Fontconfig |
| 测试 | Python 3、SPIRV-Tools（`spirv-val`） |
| 字体 | 覆盖界面语言的系统字体，中文推荐 Noto CJK |

### Gentoo

```sh
sudo emerge --ask \
  dev-build/cmake dev-build/ninja dev-util/pkgconf dev-vcs/git \
  dev-libs/wayland dev-libs/wayland-protocols dev-util/wayland-scanner \
  x11-libs/libxkbcommon '>=media-libs/glfw-3.4[wayland]' \
  '>=dev-util/vulkan-headers-1.4' '>=media-libs/vulkan-loader-1.4' \
  dev-util/glslang dev-util/spirv-tools \
  '>=media-libs/glm-1.0' dev-libs/libfmt dev-libs/spdlog \
  media-libs/assimp '>=dev-cpp/nlohmann_json-3.11' dev-libs/boost \
  dev-libs/stb media-libs/fontconfig media-fonts/noto-cjk dev-lang/python
```

选择支持 C++23 的编译器配置。`media-libs/glfw` 需要启用 `wayland` USE；Portage 提示 USE 变更时，将其写入相应的 `package.use` 配置后继续。

GPU 驱动按硬件选择：Mesa 使用 `media-libs/mesa[vulkan]` 并配置相应 `VIDEO_CARDS`；NVIDIA 使用与内核模块匹配的 `x11-drivers/nvidia-drivers`。着色器编译也可使用 `media-libs/shaderc` 提供的 `glslc`。

Vulkan 调试层由 `media-libs/vulkan-layers` 提供，供 `--validation` 使用。

### 其他发行版

按上表安装对应开发包，并核对版本和 GLFW 的 Wayland 后端：

| 发行版 | Vulkan 头文件 / loader 开发包 | Fontconfig 开发包 | 中文字体 |
| --- | --- | --- | --- |
| Debian / Ubuntu | `libvulkan-dev` | `libfontconfig1-dev` | `fonts-noto-cjk` |
| Fedora | `vulkan-headers`、`vulkan-loader-devel` | `fontconfig-devel` | `google-noto-sans-cjk-fonts` |
| Arch Linux | `vulkan-headers`、`vulkan-icd-loader` | `fontconfig` | `noto-fonts-cjk` |

## 构建

在仓库根目录执行：

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

产物为 `build/release/bin/npgs`。将上述 preset 替换为 `linux-debug` 可生成 `build/debug/` 下的 Debug 构建。默认测试覆盖 CPU、资源和 SPIR-V 检查。

GLFW、Dear ImGui、Vulkan Memory Allocator、SPIRV-Cross、magic_enum、fast-cpp-csv-parser 和 GLI 优先使用符合接口要求的系统依赖，缺失时下载固定版本源码并校验 SHA-256。Dear ImGui core 与官方 GLFW / Vulkan backend 使用同版 `v1.93.0-docking`。具体版本与检测规则见 [`NpgsDependencies.cmake`](../cmake/NpgsDependencies.cmake)。

### 离线构建

提前准备系统开发包与固定版本依赖源码，再关闭自动下载：

```sh
cmake --preset linux-release -DNPGS_FETCH_MISSING_DEPENDENCIES=OFF
```

可通过 `FETCHCONTENT_SOURCE_DIR_NPGS_IMGUI` 等 CMake 变量指定预备源码目录。支持的依赖名称为 `NPGS_GLFW`、`NPGS_IMGUI`、`NPGS_VMA`、`NPGS_SPIRV_CROSS`、`NPGS_MAGIC_ENUM`、`NPGS_CSV`、`NPGS_GLI`；目录版本应与依赖文件中的固定来源一致。

## 安装

安装到当前用户目录：

```sh
cmake --install build/release --prefix "$HOME/.local"
"$HOME/.local/bin/npgs"
```

默认布局：

```text
<prefix>/
├── bin/npgs
└── share/npgs/Assets/
```

程序按可执行文件位置查找资源，可从任意工作目录启动。自定义资源目录与字体见[使用指南](usage.md#资源与用户文件)，打包前请查阅[资源与许可证](licensing.md)。
