<div align="center">

# 🌃 NeonEngine: Cyberpunk Renderer

**A Custom Real-Time Rendering Engine Built from Scratch with C++ & OpenGL**

[![C++](https://img.shields.io/badge/C++-11%2F14%2F17-blue.svg?style=flat-square&logo=c%2B%2B)](#)
[![OpenGL](https://img.shields.io/badge/API-OpenGL-red.svg?style=flat-square&logo=opengl)](#) 
[![CMake](https://img.shields.io/badge/Build-CMake-brightgreen.svg?style=flat-square&logo=cmake)](#)
[![Dependencies](https://img.shields.io/badge/Libs-GLM%20%7C%20GLAD%20%7C%20ImGui-purple.svg?style=flat-square)](#)

*A technical showcase of a modern 3D rendering pipeline, featuring massive UE5 commercial street assets running at ~100 FPS.*

<video src="https://github.com/user-attachments/assets/65e276b1-778e-4887-81e4-2cb506dae81d" autoplay loop muted playsinline width="100%"></video>

</div>

---

## ✨ Core Features & Technical Highlights

This engine demonstrates modern real-time rendering techniques with a strong emphasis on high-contrast cyberpunk lighting, physically based wet surfaces, and GPU-driven architecture.

### 🔍 Real-Time Rendering Capabilities

| **Screen Space Reflections (SSR)** | **Volumetric Fog** |
| :---: | :---: |
| <video src="https://github.com/user-attachments/assets/397e2a93-da41-40da-88b1-c8002d03491d" autoplay loop muted playsinline width="100%"></video> | <video src="https://github.com/user-attachments/assets/b8978a0d-0155-4ddd-99de-e898c304d3b7" autoplay loop muted playsinline width="100%"></video> |
| Screen-space ray marching for accurate reflections on wet puddles and metallic surfaces. | Ray-marched volumetric fog computing scattering and attenuation from scene lights. |

| **Physical Bloom & PBR** | **Temporal Anti-Aliasing (TAA)** |
| :---: | :---: |
| <video src="https://github.com/user-attachments/assets/b8a6cdfa-b376-461b-820e-b64e349cf866" autoplay loop muted playsinline width="100%"></video> | <video src="https://github.com/user-attachments/assets/550c7a5f-1d50-475f-a2aa-1b19db36f49c" autoplay loop muted playsinline width="100%"></video> |
| Energy-conserving physical bloom combined with a comprehensive physically based shading model. | Halton-sequence jittering for temporal stability and high-frequency artifact resolution. |

---

## ⚙️ Architecture & Implementation

- **Hybrid Rendering Pipeline:** Features a G-Buffer-based Deferred Shading pipeline optimized for massive multi-light scenes, seamlessly integrated with a Forward rendering pass for translucent materials (e.g., glass).
- **GPU-Driven Particle System:** Leverages `Compute Shaders` to simulate high-density raindrop particles. Physics and collision responses are calculated entirely on the GPU against G-Buffer depth and normal data.
- **Asynchronous Asset Pipeline:** Implements a non-blocking streaming architecture utilizing `std::async` and thread-safe queues for stutter-free loading of heavyweight UE5 scene data.
- **Performance Optimization:** Incorporates Frustum Culling and UBO data streaming. Extensively profiled and debugged using RenderDoc to eliminate state leaks and rendering bottlenecks.

---

## 🚀 Getting Started

### Controls
Designed primarily as a roaming and rendering testbed:
- `W / A / S / D` - Camera Roaming
- `Mouse Scroll` - Zoom in / out
- `ImGui Panel` - Real-time toggling of post-processing effects (Bloom, SSR, SSAO, Fog, TAA, etc.)

### Build Instructions
```bash
git clone [https://github.com/SMYSN-123/CyberGL-Engine.git](https://github.com/SMYSN-123/CyberGL-Engine.git)
cd CyberGL-Engine
mkdir build && cd build
cmake ..
cmake --build .
