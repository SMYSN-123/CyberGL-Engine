#ifndef RAINY_ALLEY_SCENE_H
#define RAINY_ALLEY_SCENE_H

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include "Shader.h"
#include "Model.h"

class RainyAlleyScene
{
public:
    RainyAlleyScene() : cityPart1(nullptr), cityPart2(nullptr) {}

    ~RainyAlleyScene() {
        if (cityPart1) delete cityPart1;
        if (cityPart2) delete cityPart2;
        if (cityPart3) delete cityPart3;
        if (cityPart4) delete cityPart4;
    }

    void Init()
    {
        // ==========================================
        // 🚀 工业级加载：一行代码顶过去几百行手工搭建
        // ==========================================
        cityPart1 = new Model("../extern/UE_Source/RainyStreet/Modular/Modular.gltf");
        cityPart2 = new Model("../extern/UE_Source/RainyStreet/Planes/Planes.gltf");
        cityPart3 = new Model("../extern/UE_Source/RainyStreet/Splines/Splines.gltf");
        cityPart4 = new Model("../extern/UE_Source/RainyStreet/Props/Props.gltf");

        std::cout << "Part 1 Mesh 数量: " << cityPart1->meshes.size() << std::endl;
        std::cout << "Part 2 Mesh 数量: " << cityPart2->meshes.size() << std::endl;
        std::cout << "Part 3 Mesh 数量: " << cityPart3->meshes.size() << std::endl;
        std::cout << "Part 4 Mesh 数量: " << cityPart4->meshes.size() << std::endl;
    }

    // ==========================================
    // 🌟 1. 仅绘制不透明物体 (用于 G-Buffer Pass)
    // ==========================================
    void DrawOpaque(const Shader& shader, const Camera& camera, float screenWidth, float screenHeight)
    {
        // if (!cityPart1 || !cityPart2) return;

        Frustum frustum = createFrustumFromCamera(camera, screenWidth / screenHeight, glm::radians(camera.Zoom), 0.1f, 1000.0f);
        glm::mat4 globalModelMatrix = glm::mat4(1.0f);

        shader.setMat4("model", globalModelMatrix);
        shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(globalModelMatrix))));

        unsigned int totalProcessed = 0;
        unsigned int totalSentToGPU = 0;

        // 🌟🌟🌟 核心修复：必须先收集并排序半透明网格！否则什么都画不出来！🌟🌟🌟
        if (cityPart1) {
            cityPart1->CollectAndSortTransparentMeshes(camera.Position);
        }
        if (cityPart2) {
            cityPart2->CollectAndSortTransparentMeshes(camera.Position);
        }
        if (cityPart3) {
            cityPart3->CollectAndSortTransparentMeshes(camera.Position);
        }
        if (cityPart4) {
            cityPart4->CollectAndSortTransparentMeshes(camera.Position);
        }
        // 🌟 调用 Model 中的 DrawOpaquePBR_Culled
        cityPart1->DrawOpaquePBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart2->DrawOpaquePBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart3->DrawOpaquePBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart4->DrawOpaquePBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);

        PrintCullingLog("Opaque", totalProcessed, totalSentToGPU);
    }

    // ==========================================
    // 🌟 2. 仅绘制半透明物体 (用于 Forward Transparent Pass)
    // ==========================================
    void DrawTransparent(const Shader& shader, const Camera& camera, float screenWidth, float screenHeight)
    {
        // if (!cityPart1 || !cityPart2) return;

        Frustum frustum = createFrustumFromCamera(camera, screenWidth / screenHeight, glm::radians(camera.Zoom), 0.1f, 1000.0f);
        glm::mat4 globalModelMatrix = glm::mat4(1.0f);

        shader.setMat4("model", globalModelMatrix);
        shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(globalModelMatrix))));

        unsigned int totalProcessed = 0;
        unsigned int totalSentToGPU = 0;

        // 🌟 调用 Model 中的 DrawTransparentPBR_Culled
        cityPart1->DrawTransparentPBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart2->DrawTransparentPBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart3->DrawTransparentPBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart4->DrawTransparentPBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
    }

    // 保留原有的 Draw 函数以兼顾兼容性
    void Draw(const Shader& shader, const Camera& camera, float screenWidth, float screenHeight)
    {
        DrawOpaque(shader, camera, screenWidth, screenHeight);
    }

private:
    void PrintCullingLog(const char* passName, unsigned int totalProcessed, unsigned int totalSentToGPU)
    {
        if (totalProcessed > 0)
        {
            static int frameCounter = 0;
            if (frameCounter % 60 == 0)
            {
                float savedPercentage = (1.0f - (float)totalSentToGPU / totalProcessed) * 100.0f;
                std::cout << "[" << passName << " Frustum Culling] Total: " << totalProcessed 
                            << " | Rendered: " << totalSentToGPU 
                            << " | Saved: " << savedPercentage << "%" << std::endl;
            }
            frameCounter++;
        }
    }

    Model* cityPart1;
    Model* cityPart2;
    Model* cityPart3;
    Model* cityPart4;
};

#endif