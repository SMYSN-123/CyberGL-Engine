<div align="center">

# 🌃 NeonEngine: Cyberpunk Renderer

**A Custom Real-Time Rendering Engine Built from Scratch with C++ & OpenGL**

[![C++](https://img.shields.io/badge/C++-11%2F14%2F17-blue.svg?style=flat-square&logo=c%2B%2B)](#)
[![OpenGL](https://img.shields.io/badge/API-OpenGL-red.svg?style=flat-square&logo=opengl)](#) 
[![CMake](https://img.shields.io/badge/Build-CMake-brightgreen.svg?style=flat-square&logo=cmake)](#)
[![Dependencies](https://img.shields.io/badge/Libs-GLM%20%7C%20GLAD%20%7C%20ImGui-purple.svg?style=flat-square)](#)

*A technical showcase of modern 3D rendering pipelines, loading massive UE5 commercial street assets running at ~100 FPS.*

<!-- 15秒无 UI 漫游视频 -->
<video src="https://github.com/SMYSN-123/CyberGL-Engine/raw/main/media/2026-09-09%2019-28-36.mp4" autoplay loop muted playsinline width="100%"></video>

</div>

---

## ✨ Core Features & Technical Highlights

This project is built to demonstrate modern real-time rendering techniques, focusing on high-contrast cyberpunk lighting, wet surfaces, and GPU-driven systems.

### 🔍 Interactive Rendering Debugging
<!-- 【这里放你的 4 个 ImGui 对比视频，2x2 矩阵排列】 -->
| **Screen Space Reflections (SSR)** | **Volumetric Fog** |
| :---: | :---: |
| <video src="https://github.com/SMYSN-123/CyberGL-Engine/raw/main/media/2026-09-09%2016-09-25.mp4" autoplay loop muted playsinline width="100%"></video> | <video src="https://github.com/SMYSN-123/CyberGL-Engine/raw/main/media/2026-09-10%2012-59-11.mp4" autoplay loop muted playsinline width="100%"></video> |

| **Physical Bloom & PBR** | **Temporal Anti-Aliasing (TAA)** |
| :---: | :---: |
| <video src="https://github.com/SMYSN-123/CyberGL-Engine/raw/main/media/2026-09-10%2012-58-47.mp4" autoplay loop muted playsinline width="100%"></video> | <video src="https://github.com/SMYSN-123/CyberGL-Engine/raw/main/media/2026-09-09%2016-14-34.mp4" autoplay loop muted playsinline width="100%"></video> |
| Energy-conserving bloom combined with fully physical-based shading. | Halton-sequence jittering for temporal stability and resolving high-frequency artifacts. |

---

## ⚙️ Architecture & Implementation

- **Hybrid Rendering Pipeline:** Implemented a G-Buffer based Deferred Shading pipeline for massive multi-light scenes, combined with Forward rendering for translucent materials (glass).
- **GPU-Driven Particle System:** Utilized `Compute Shader` to simulate massive raindrop particles, performing physics and collision response directly against G-Buffer normals and depth.
- **Asynchronous Asset Pipeline:** Built a non-blocking streaming system using `std::async` and queues to load massive UE5 scene data smoothly.
- **Optimizations:** Implemented Frustum Culling, UBO data streaming, and rigorously debugged state leaks using RenderDoc.

---

## 🚀 Getting Started

### Controls
Designed primarily as a roaming and rendering testbed:
- `W / A / S / D` - Camera Roaming
- `Mouse Scroll` - Zoom in / out
- `ImGui Panel` - Real-time toggling of post-processing effects (Bloom, SSR, SSAO, Fog, etc.)

### Build Instructions
```bash
git clone https://github.com/SMYSN-123/CyberGL-Engine.git
cd CyberGL-Engine
mkdir build && cd build
cmake ..
cmake --build .
