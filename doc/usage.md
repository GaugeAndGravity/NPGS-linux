# 使用指南

[返回首页](../README.md) · [构建与安装](build.md)

## 启动与菜单

在已登录的 Wayland 会话中运行：

```sh
./build/release/bin/npgs
```

默认窗口请求为 1280×960。在主菜单选择“新的模拟”→“开始模拟”进入主界面。窗口尺寸由 Wayland compositor 最终决定，平铺桌面可将 NPGS 窗口设为浮动以便自由缩放；Wayland `app_id` 为 `npgs`。

### 命令行参数

```sh
./build/release/bin/npgs --help
./build/release/bin/npgs --width 960 --height 720
```

| 参数 | 用途 |
| --- | --- |
| `--help` / `-h` | 显示帮助，保持纯命令行操作 |
| `--width N`、`--height N` | 请求窗口宽、高，取正整数 |
| `--fullscreen` | 全屏交互运行 |
| `--frames N` | 成功呈现 N 帧后退出，窗口模式；未指定的宽、高分别为 320、240 |
| `--smoke-resize` | 有限帧模式中测试窗口与 swapchain 缩放，配合 `--frames N` 且 N ≥ 4 |
| `--validation` | 启用已安装的 Khronos Vulkan validation layer |

带值参数使用空格分隔，例如 `--width 960`。退出码：`0` 为正常完成，`2` 为参数错误，`1` 为启动或渲染异常。

## 吸积盘与参数

吸积盘内半径 `InterRadiusRs` 的默认值 `200` 会隐藏盘面。观察吸积盘时，先在参数界面调小内半径。

按 `O` 循环选择 `Assets/Textures/Disk/` 中的图片盘；循环结束恢复原有体积盘参数。

## 键鼠操作

操作场景前先将焦点移出文本框；UI 捕获输入时，场景快捷键会让位于界面操作。

| 输入 | 行为 |
| --- | --- |
| `T` | 切换绕转 / 自由相机，适用于普通相机模式 |
| `W` / `S`、`A` / `D`（绕转） | 纵向、横向环绕 |
| 鼠标左键拖动 | 绕转相机环绕；自由相机转动视角 |
| 鼠标右键拖动（绕转） | 摆头 |
| 鼠标中键（绕转） | 平滑回正摆头 |
| `W` / `S`、`A` / `D`、`R` / `F`（自由） | 前后、左右、上下移动 |
| `Q` / `E`（自由） | 左右滚转 |
| `G` | 开关测地线模式，进入时自动切换自由相机 |
| `W S A D R F`（测地线） | 沿相机局部方向施加推力 |
| 滚轮 | 绕转模式调整轨道半径；自由模式调整移动速度；测地线模式调整推力 |
| `H` | 切换 UI 与鼠标显示 |
| `O` | 循环切换图片吸积盘 |
| `Ctrl` + `Alt` + `S` | 启动高分辨率批量截图，计算量较大 |
| `P` | 导出当前着色器参数 |
| `Esc` | 关闭窗口 |

按住 `Ctrl` 或 `Alt` 时，普通移动和滚转键让位于组合快捷键。桌面全局快捷键的优先级由 compositor 配置决定。

### 滚轮调整幅度

在主菜单的“设置”，或游戏内“管理 / 网络”终端的 `CUSTOM` 设置页中，使用“滚轮调整幅度 (%)”滑块或直接输入数值。范围为 0–100%，默认 20%，三个相机模式共享，修改实时生效；返回主菜单仍保留，重启程序后恢复默认值。

该百分比按 GLFW 滚轮单位计算，增大倍率为 `(1 + 百分比 / 100)^滚轮偏移`，反向使用倒数倍率，因此来回滚动可以恢复原值。设备与桌面设置可能使物理滚动一格产生非整数单位：例如一格为 1.5 单位时，20% 对应约 1.3145 倍，改为 5% 则约为 1.0759 倍。设为 0 可禁用场景中的滚轮调整，不影响 UI 列表滚动。

### 测地线位置命令

在 UI 命令输入区域使用：

```text
/tp x y z t vx vy vz
```

七个数值用于在测地线模式下设置位置、坐标时间与空间四速度分量。时间分量按 ingoing Kerr–Schild 坐标约定求解。

## 字体

程序通过 Fontconfig 匹配 `sans-serif regular` 主字体，实际文件由系统字体配置决定。中文与 UI 符号的缺字由 Fontconfig 排序得到的系统字体补齐。中文推荐安装 Noto CJK；Gentoo 包名为 `media-fonts/noto-cjk`。

Dear ImGui 使用官方动态字体机制按需加载字形，由官方 Vulkan backend 处理纹理创建、更新与退休。栅格密度随实际 framebuffer scale 调整，字符覆盖取决于已安装的字体。

如需指定主字体，可设置 `NPGS_FONT` 为可读 TTF / OTF / TTC 文件的绝对路径，系统缺字 fallback 继续生效：

```sh
NPGS_FONT="$HOME/.local/share/fonts/MyFont.ttf" ./build/release/bin/npgs
```

显式指定 TTC 文件时使用 face 0；系统自动匹配保留 Fontconfig 选择的 face index。

## 资源与用户文件

构建树和安装树均使用 `bin/npgs` 与 `share/npgs/Assets/` 的相对布局。`NPGS_ASSET_DIR` 可覆盖资源根目录，值为已有 `Assets` 目录的绝对路径：

```sh
NPGS_ASSET_DIR="$HOME/.local/share/npgs/Assets" ./build/release/bin/npgs
```

自定义资源目录应包含 UI、翻译、纹理、数据表与当前构建生成的 SPIR-V。

| 文件 | 默认位置 |
| --- | --- |
| 截图 | `${XDG_STATE_HOME:-$HOME/.local/state}/npgs/Screenshot_Batch_时间戳/` |
| 参数导出 | `${XDG_STATE_HOME:-$HOME/.local/state}/npgs/ShaderArgs_帧号.json` |
| 着色器缓存 | `${XDG_CACHE_HOME:-$HOME/.cache}/npgs/Shaders/` |

显式设置的 XDG 目录需为绝对路径。

## 常见问题

- **GLFW 初始化失败**：核对当前 Wayland 会话、`WAYLAND_DISPLAY`、`XDG_RUNTIME_DIR`，以及 GLFW 的 Wayland 后端。
- **Vulkan 设备或格式不可用**：核对驱动、Vulkan 1.4、统一 graphics / compute / present 队列族，以及[支持的 surface 格式](build.md#平台与工具)。
- **资源加载失败**：核对 `share/npgs/Assets/` 布局与 `NPGS_ASSET_DIR`；重新构建以生成 SPIR-V。
- **中文或符号缺字**：安装相应语言字体，或用 `NPGS_FONT` 选择覆盖所需字符的字体。
- **图片盘不可见**：调小内半径（默认 200），再用 `O` 选择图片盘。
