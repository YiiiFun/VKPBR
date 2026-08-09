<div align="center">

# VKPBR

**用于实践 PBR、混合 Ray Query、IBL 与时域重建的 Vulkan 实时渲染器**

[English](README.md) | [简体中文](README_zh-CN.md)

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-AC162C?logo=vulkan&logoColor=white)
![Slang](https://img.shields.io/badge/shaders-Slang-6A5ACD)
![CMake](https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white)

</div>

VKPBR 是一个使用 C++20 编写的桌面实时渲染器，用于系统实践现代 Vulkan 渲染技术。项目以 Khronos Vulkan Tutorial 中采用 Apache-2.0 授权的 **Building a Simple Game Engine** 教程代码为起点，继续扩展了 glTF 资源管线、光栅化与 Compute Ray Query 双路径、分块光源裁剪、基于图像的光照、环境光遮蔽、时域抗锯齿和 ImGui 实时调试界面。

## 功能亮点

- **双渲染路径：** 可在运行时切换光栅化和 Compute Ray Query；支持的设备会按需构建 BLAS/TLAS 加速结构。
- **glTF PBR 管线：** 支持 glTF/GLB 场景、金属度-粗糙度与高光-光泽度工作流、法线/AO/自发光纹理、Alpha 模式、透射、相机、灯光、实例与变换动画。
- **混合光线效果：** 可调硬阴影、多样本软阴影、反射、透明/折射、二次反弹，以及可选的厚玻璃吸收近似。
- **可扩展光照：** Forward+ Tile 光源裁剪、GPU 光源列表、具名灯组，以及面向 Bistro 场景的日景/夜景控制。
- **基于图像的光照：** 在 GPU 上把 HDR 等距柱状图转换为环境立方体贴图、漫反射辐照度图、高光预过滤图和 BRDF 积分 LUT。
- **屏幕空间与时域效果：** SSAO、GTAO、双边 AO 模糊、TAA 重投影、深度拒绝、邻域裁剪、锐化和对应调试视图。
- **运行时资源处理：** 异步纹理流送、KTX2/BasisU、Mip 生成、各向异性采样、加载进度显示和 Vulkan 资源延迟销毁。
- **可见性与调试：** CPU 视锥裁剪、基于投影尺寸的距离 LOD、透明物体排序、固定相机对比，以及 ImGui 渲染器/相机参数面板。

## 渲染效果

<img src="docs/images/bistro-day-ibl.jpg" alt="启用 Ray Query 光照与 HDR IBL 的 Bistro 日景" width="100%">

<p align="center"><b>Ray Query · 日景</b><br>硬阴影、反射/折射与 Split-Sum HDR IBL。</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/bistro-day-ray-query.jpg" alt="启用硬阴影、反射和折射的 Bistro 日景"></td>
    <td width="50%"><img src="docs/images/bistro-night-ray-query.jpg" alt="启用局部灯光、Ray Query 阴影和 Mipmap 的 Bistro 夜景"></td>
  </tr>
  <tr>
    <td align="center"><b>Ray Query · 直接光照</b><br>硬阴影与反射/折射查询</td>
    <td align="center"><b>Ray Query · 夜景</b><br>路灯分组、局部阴影与 Mipmap 采样</td>
  </tr>
</table>

<details>
<summary><b>光栅化与相机实时控制</b></summary>
<br>
<img src="docs/images/bistro-raster-controls.jpg" alt="光栅化 Phong 基线与 Vulkan 渲染器、相机控制面板" width="100%">
<p align="center">光栅化 Phong 基线，以及固定相机、剔除/LOD 和纹理采样控制。</p>
</details>

以上截图使用相同相机位置，便于直接比较光照变化。TAA 的主要收益是运动过程中的时域稳定性，因此不使用单张静态截图作为效果证明。

## 渲染流程

```text
glTF / GLB 场景 + HDR 环境图
              │
              ▼
 网格、材质、纹理与光源提取
              │
      ┌───────┴────────┐
      ▼                ▼
   光栅化路径       BLAS / TLAS 构建
深度预通过 + PBR    Compute Ray Query
      │                │
      └───────┬────────┘
              ▼
       SSAO / GTAO → TAA
              │
              ▼
       色调映射 → 交换链
              │
              ▼
        ImGui 实时诊断
```

光栅化路径保留了基础 Phong/Blinn-Phong 对比，并提供 GGX 风格的 PBR 管线。Ray Query 路径在 Compute Shader 中使用 Vulkan 加速结构查询可见性、反射与折射。两条路径最终进入相同的 AO、时域重建和显示阶段，因此可以在固定机位下进行 A/B 对比。

## 编译与运行

### 环境要求

- Windows 10/11
- 支持 Vulkan 1.3 的 GPU 与较新的显卡驱动
- CMake 3.28 或更高版本，以及 Ninja
- C++20 编译器；仓库预设使用 MSYS2 MinGW-w64 GCC
- GLFW、GLM、tinygltf、KTX 与 Vulkan 开发包
- `slangc`；当前本地预设从 `.tools/` 读取，也可以由 Vulkan SDK 提供

缺少可选的 `VK_KHR_ray_query` 和 `VK_KHR_acceleration_structure` 时仍可使用光栅化路径。Ray Query 模式同时要求这两个扩展、Buffer Device Address 和 Shader `int64` 支持。

### 终端运行

```powershell
git clone https://github.com/YiiiFun/VKPBR.git
cd VKPBR

cmake --preset mingw64
cmake --build --preset mingw64-debug
.\build\mingw64\bin\SimpleEngine.exe
```

当前预设假定 MSYS2 安装在 `C:\msys64\mingw64`。如果工具链或 Vulkan SDK 位于其他目录，需要先修改 `CMakePresets.json`。构建完成后，CMake 会把 Shader、资源和 MinGW 运行库复制到可执行文件旁边。

### VS Code

使用 VS Code 打开仓库根目录，在“运行和调试”中选择 **Debug VKPBR (MinGW64)**。现有配置会在启动 GDB 前自动执行 CMake 配置与编译；换到另一台机器时，可能需要调整 `.vscode/tasks.json`、`.vscode/launch.json` 和 `.vscode/settings.json` 中的工具路径。

## 运行时控制

| 操作 | 作用 |
| --- | --- |
| `W/A/S/D`、`Space`、`Shift` | 控制自由相机移动 |
| 按住鼠标左键拖动 | 旋转相机 |
| 按住鼠标右键拖动 | 平移相机 |
| 鼠标滚轮 | 前后推拉相机 |
| `U` | 显示或隐藏 ImGui 界面 |
| **Mode** | 在硬件支持时切换光栅化与 Ray Query |
| **Fixed Camera** | 固定机位，便于重复对比 |
| **AO Mode** | 切换 Off、SSAO 或 GTAO |
| **TAA debug view** | 查看最终结果、运动矢量、历史权重或反遮挡区域 |
| **IBL debug view** | 查看环境图、辐照度图、预过滤图或 BRDF LUT |
| **Day/Night preset** | 在 Ray Query 模式下设置 Bistro 灯组 |

HDR IBL 与 TAA 默认关闭，使程序首先呈现稳定基线；需要时可在 Renderer 面板中分别开启。

默认模型路径在 [`main.cpp`](main.cpp) 中选择。Bistro 加载语句旁边已经列出其他测试场景，每次启用一个即可。

## 项目结构

```text
.
├── main.cpp                     # 程序入口与默认场景选择
├── engine.*                     # 主循环、实体、相机输入与 UI
├── renderer*.cpp / renderer.h   # 按职责拆分的 Vulkan 渲染器
├── model_loader.*               # glTF/GLB 材质、网格、灯光与动画
├── scene_loading.*              # 后台场景构建与上传调度
├── resource_manager.*           # 纹理任务与资源生命周期
├── shaders/                     # Slang 光栅、Compute、AO、IBL 与 TAA Shader
├── Assets/                      # 测试场景、纹理与 HDR 环境图
├── CMake/                       # 依赖查找模块
├── external/                    # 随仓库提供的 tinygltf 与 stb 头文件
├── imgui/                       # Dear ImGui 源码
├── Courses/                     # 可选功能模块，默认不参与构建
└── docs/images/                 # README 场景图与截图清单
```

## 前置知识

- [Khronos Vulkan Tutorial](https://docs.vulkan.org/tutorial/latest/00_Introduction.html)
- [Vulkan Ray Queries](https://docs.vulkan.org/spec/latest/chapters/raytraversal.html)
- [glTF 2.0 规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [Physically Based Rendering — Filament](https://google.github.io/filament/Filament.html)

## 项目范围

这是一个用于学习和作品集展示的渲染器，不是通用游戏引擎或场景编辑器。当前代码面向桌面 Vulkan 1.3，场景路径在编译时选择，渲染实验通过 ImGui 暴露。Ray Query 依赖硬件能力，平面反射目前关闭，可选课程模块也不会进入默认构建。

## 许可证与署名

主体源码与 Shader 保留了 Holochip Corporation 原有的版权声明和 Apache-2.0 SPDX 头。项目修改部分沿用 Apache-2.0 发布，详见 [`LICENSE`](LICENSE) 与 [`NOTICE`](NOTICE)。再次分发源码时不要删除这些文件头。

第三方库和模型资源沿用各自许可证，详见 [`THIRD_PARTY_ASSETS.md`](THIRD_PARTY_ASSETS.md) 以及依赖和模型目录中保留的授权文件。
