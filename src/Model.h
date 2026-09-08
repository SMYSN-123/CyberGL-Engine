#ifndef MODEL_H
#define MODEL_H

#include <iterator>
#include <future>
#include <stb_image.h>
#include "Mesh.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// 1. 定义一个结构体，用来装后台线程解压出来的裸数据
struct TextureData
{
    unsigned char* pixels;
    int width, height, nrComponents;
    std::string path;
    std::string typeName; // 保存贴图类型 (diffuse, normal 等)
};

// 🌟 [新增] Assimp 矩阵转 GLM 矩阵的内联辅助函数
inline glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4& from)
{
    glm::mat4 to;
    // 注意：Assimp 是行主序，GLM 是列主序，所以这里在赋值时发生了转置
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    return to;
}

inline std::string urlDecode(const std::string& str) {
    std::string ret;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int hex;
            sscanf_s(str.substr(i + 1, 2).c_str(), "%x", &hex);
            ret += static_cast<char>(hex);
            i += 2;
        } else {
            ret += str[i];
        }
    }
    return ret;
}

struct TransparentMesh
{
    const Mesh* mesh;
    glm::vec3 position;
    float distance;
};

class Model
{
public:
    Model(const char* path)
    {
        // 🌟 将贴图翻转设置放在构造函数里，避免多线程同时修改 stb 的全局状态导致崩溃
        stbi_set_flip_vertically_on_load(true);
        loadModel(path);
    }

    // 第一遍：收集所有透明物体的距离
    void CollectTransparentMeshes(const glm::vec3& viewPos)
    {
        transparentMeshes.clear();
        
        for (auto& mesh : meshes) {
            if (mesh.isTransparent) {  // 这里假设 Mesh 有 isTransparent 标记
                // 直接使用 AABB 已经算好的 center 属性
                glm::vec3 meshCenter = mesh.boundingBox.center;
                float dist = glm::distance(viewPos, meshCenter);
                
                transparentMeshes.push_back({&mesh, meshCenter, dist});
            }
        }
        
        // 从后往前排序（远的先画，近的后画）
        std::sort(transparentMeshes.begin(), transparentMeshes.end(),
            [](const TransparentMesh& a, const TransparentMesh& b) {
                return a.distance > b.distance;  // 降序：远的先
            });
    }


    // 🌟🌟🌟 终极渲染核心：带有视锥体剔除和性能统计的绘制 🌟🌟🌟
    void DrawPBR_Culled(const Shader& shader, const Frustum& frustum, const glm::mat4& modelMatrix, unsigned int& display, unsigned int& total)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            total++; // 记录总 Mesh 数量

            // 拿着局部 AABB 和 当前模型的矩阵，去和视锥体做生死判决！
            if(meshes[i].boundingBox.isOnFrustum(frustum, modelMatrix))
            {
                meshes[i].DrawPBR(shader);
                display++; // 活下来的 Mesh 数量
            }
        }
    }

    void Draw(Shader& shader)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].Draw(shader);
        }
    }

    // void DrawPBR(const Shader& shader)
    // {
    //     for(unsigned int i = 0; i < meshes.size(); i++)
    //     {
    //         meshes[i].DrawPBR(shader);
    //     }
    // }

    // 🌟 改进的 Draw 函数：分别渲染不透明和透明物体
    void DrawPBR(const Shader& shader, const glm::vec3& viewPos)
    {
        // ========== 第一遍：渲染所有不透明物体 ==========
        glDepthMask(GL_TRUE);  // 写入深度缓冲
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        for (const auto& mesh : meshes) {
            if (!mesh.isTransparent) {
                mesh.DrawPBR(shader);
            }
        }
        
        // ========== 第二遍：收集并排序透明物体 ==========
        CollectAndSortTransparentMeshes(viewPos);
        
        // ========== 第三遍：从后往前渲染透明物体 ==========
        glDepthMask(GL_FALSE);  // ⚠️ 关键：不写入深度，只读
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        for (const auto& transparentMesh : transparentMeshes) {
            transparentMesh.mesh->DrawPBR(shader);
        }
        
        // 恢复深度写入
        glDepthMask(GL_TRUE);
    }

    void DrawInstanced(Shader& shader, unsigned int amount)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].DrawInstanced(shader, amount);
        }
    }

    void ConfigureInstancedArray(unsigned int instanceVBO)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
        meshes[i].SetupInstancedAttributes(instanceVBO);
        }
    }

    // 🌟 [新增] 纯净版绘制
    void DrawGeometryOnly() 
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].DrawGeometryOnly();
        }
    }

    // 🌟 只绘制不透明物体（用于 G-Buffer Pass）
    void DrawOpaquePBR_Culled(const Shader& shader, const Frustum& frustum, const glm::mat4& modelMatrix, unsigned int& display, unsigned int& total)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            // ⚠️ 跳过半透明物体
            if (meshes[i].isTransparent) continue;

            total++;
            if(meshes[i].boundingBox.isOnFrustum(frustum, modelMatrix))
            {
                meshes[i].DrawPBR(shader);
                display++;
            }
        }
    }

    // 🌟 修复：改用排序好的 transparentMeshes 进行剔除绘制
    void DrawTransparentPBR_Culled(const Shader& shader, const Frustum& frustum, const glm::mat4& modelMatrix, unsigned int& display, unsigned int& total) {
        static bool printedOnce = false; 
        int transparentMeshCount = 0;

        for(unsigned int i = 0; i < transparentMeshes.size(); i++) {
            transparentMeshCount++;
            total++;
            if(transparentMeshes[i].mesh->boundingBox.isOnFrustum(frustum, modelMatrix)) {
                transparentMeshes[i].mesh->DrawPBR(shader);
                display++;
            }
        }

        if (!printedOnce) {
            std::cout << "==========================================" << std::endl;
            std::cout << "[Forward Pass 渲染诊断] 场景共有 " << meshes.size() << " 个 Mesh" << std::endl;
            std::cout << "[Forward Pass 渲染诊断] 识别到半透明 Mesh 数量: " << transparentMeshes.size() << " 个" << std::endl;
            std::cout << "==========================================" << std::endl;
            printedOnce = true;
        }
    }

    // 🌟 新增函数：收集透明物体并按距离排序
    void CollectAndSortTransparentMeshes(const glm::vec3& viewPos)
    {
        transparentMeshes.clear();
        
        for (const auto& mesh : meshes) {
            // 只收集透明物体（玻璃、发光体等）
            if (mesh.isTransparent) {
                // 计算 Mesh 的中心点
                // 直接使用 AABB 已经算好的 center 属性
                glm::vec3 meshCenter = mesh.boundingBox.center;
                
                // 计算与摄像机的距离
                float distance = glm::distance(viewPos, meshCenter);
                
                transparentMeshes.push_back({
                    &mesh,
                    meshCenter,
                    distance
                });
            }
        }
        
        // 🌟 关键：从后往前排序（远的先渲染，近的后渲染）
        // 这样确保近处的透明物体会正确覆盖远处的
        std::sort(transparentMeshes.begin(), transparentMeshes.end(),
            [](const TransparentMesh& a, const TransparentMesh& b) {
                return a.distance > b.distance;  // 降序排列
            });
    }

    std::vector<Mesh> meshes;
    std::vector<TransparentMesh> transparentMeshes;
private:
    std::string directory;
    std::vector<Texture> textures_loaded;

    void loadModel(const std::string& path)
    {
        Assimp::Importer import;
        const aiScene* scene = import.ReadFile(path, 
            aiProcess_Triangulate |           // 保证是三角形
            aiProcess_CalcTangentSpace |      // 算切线 (因为没用 FlipUVs，切线空间绝对正确)
            aiProcess_JoinIdenticalVertices | // 合并顶点
            aiProcess_GenSmoothNormals
            // 🌟 核心修复 1：强制 Assimp 烘焙 UE5 的 UV 缩放和偏移，解决纹理巨大/错位问题！
            // aiProcess_TransformUVCoords
        );

        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cout << "ERROR::ASSIMP::" << import.GetErrorString() << "\n";
            return;
        }
        directory = path.substr(0, path.find_last_of('/'));

        processNode(scene->mRootNode, scene, glm::mat4(1.0f));
    }

    void processNode(aiNode* node, const aiScene* scene, glm::mat4 parentTransform)
    {
        // 计算当前节点的绝对变换矩阵
        glm::mat4 currentTransform = parentTransform * aiMatrix4x4ToGlm(node->mTransformation);

        for(unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene, currentTransform));
        }

        for(unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene, currentTransform);
        }
    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene, glm::mat4 transform)
    {
        if (mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString matName;
            material->Get(AI_MATKEY_NAME, matName);
        }

        aiString matName;

        // 🌟🌟🌟 新增：动态查询当前材质的基础贴图使用了第几个 UV 通道 🌟🌟🌟
        int uvChannelToUse = 0; // 默认兜底使用 0 通道
        if (mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            
            // 向 Assimp 询问：BASE_COLOR 贴图(glTF标准)到底映射在哪个 UV 通道上？
            if (material->Get(AI_MATKEY_UVWSRC(aiTextureType_BASE_COLOR, 0), uvChannelToUse) != AI_SUCCESS)
            {
                // 如果没找到 BASE_COLOR 的映射，尝试询问传统 DIFFUSE 的映射
                material->Get(AI_MATKEY_UVWSRC(aiTextureType_DIFFUSE, 0), uvChannelToUse);
            }
        }

        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::vector<Texture> textures;

        // 🌟🌟🌟 核心新增：初始化 AABB 的极值边界 🌟🌟🌟
        // 注意避坑：求最大值要用极小值初始化，求最小值要用极大值初始化
        // 绝对不要用 std::numeric_limits<float>::min()，它是最小的正数，应该用 lowest()！
        glm::vec3 minAABB(std::numeric_limits<float>::max());
        glm::vec3 maxAABB(std::numeric_limits<float>::lowest());

        // 🌟 核心：计算用于转换法线的正规矩阵 (Normal Matrix)
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

        for(unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;

            // // 🌟🌟🌟 核心修改 1：把位置从局部空间转换到世界空间 🌟🌟🌟
            // glm::vec4 worldPos = transform * glm::vec4(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
            // vertex.Position = glm::vec3(worldPos);

            // ✅ 正确写法：AABB 用局部坐标，顶点 Position 用世界坐标
            glm::vec3 localPos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            glm::vec4 worldPos = transform * glm::vec4(localPos, 1.0f);
            vertex.Position = glm::vec3(worldPos); // 顶点继续用世界坐标

            // 🌟🌟🌟 核心新增：动态更新边界极点 🌟🌟🌟
            // 每次拿到一个顶点的世界坐标，就去挑战当前的极限记录
            minAABB.x = std::min(minAABB.x, vertex.Position.x);
            minAABB.y = std::min(minAABB.y, vertex.Position.y);
            minAABB.z = std::min(minAABB.z, vertex.Position.z);

            maxAABB.x = std::max(maxAABB.x, vertex.Position.x);
            maxAABB.y = std::max(maxAABB.y, vertex.Position.y);
            maxAABB.z = std::max(maxAABB.z, vertex.Position.z);

            // 🌟🌟🌟 核心修改 2：转换法线朝向 🌟🌟🌟
            glm::vec3 worldNormal = normalMatrix * glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            vertex.Normal = glm::normalize(worldNormal);

            // 🌟🌟🌟 核心修复：使用动态获取的 uvChannelToUse 提取 UV 坐标 🌟🌟🌟
            // 这样对于风扇它会自动取 1，对于窗户会自动取 2，对于灯牌会自动取 0
            if (mesh->mTextureCoords[uvChannelToUse]) 
            {
                vertex.TexCoords = glm::vec2(
                    mesh->mTextureCoords[uvChannelToUse][i].x, 
                    mesh->mTextureCoords[uvChannelToUse][i].y
                );
            }
            else if (mesh->mTextureCoords[0]) 
            {
                // 极端情况下的安全兜底：如果材质指定的通道不存在，强制用 0 通道
                vertex.TexCoords = glm::vec2(
                    mesh->mTextureCoords[0][i].x, 
                    mesh->mTextureCoords[0][i].y
                );
            }
            else
            {
                // 模型完全没有 UV
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }

            // 🌟🌟🌟 核心修改 3：转换切线和副切线 🌟🌟🌟
            if (mesh->HasTangentsAndBitangents())
            {
                glm::vec3 worldTangent = normalMatrix * glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                vertex.Tangent = glm::normalize(worldTangent);

                glm::vec3 worldBitangent = normalMatrix * glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
                vertex.Bitangent = glm::normalize(worldBitangent);
            }
            else
            {
                vertex.Tangent = glm::vec3(0.0f);
                vertex.Bitangent = glm::vec3(0.0f);
            }

            vertices.push_back(vertex);
        }

        for(unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
            {
                indices.push_back(face.mIndices[j]);
            }
        }

        if(mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            // ✅ 1. BaseColor：优先 glTF BASE_COLOR，不存在才 fallback 到 DIFFUSE
            //    两者只加载一次，解决重复 push 问题
            std::vector<Texture> baseColorMaps = loadMaterialTextures(
                material, aiTextureType_BASE_COLOR, "texture_diffuse");
            if (baseColorMaps.empty()) {
                baseColorMaps = loadMaterialTextures(
                    material, aiTextureType_DIFFUSE, "texture_diffuse");
            }
            textures.insert(textures.end(),
                std::make_move_iterator(baseColorMaps.begin()),
                std::make_move_iterator(baseColorMaps.end()));

            // ✅ 2. 法线贴图
            std::vector<Texture> normalMaps = loadMaterialTextures(
                material, aiTextureType_NORMALS, "texture_normal");
            if (normalMaps.empty()) {
                normalMaps = loadMaterialTextures(
                    material, aiTextureType_HEIGHT, "texture_normal");
            }
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

            // ✅ 3. 自发光贴图
            std::vector<Texture> emissiveMaps = loadMaterialTextures(
                material, aiTextureType_EMISSIVE, "texture_emissive");
            textures.insert(textures.end(), emissiveMaps.begin(), emissiveMaps.end());

            // ✅ 4. ORM 打包贴图（尝试从 UNKNOWN 槽位读取）
            std::vector<Texture> ormMaps = loadMaterialTextures(
                material, aiTextureType_UNKNOWN, "texture_metallic");

            // ✅ 5. 兼容 Assimp 对 glTF 的处理：ORM 通常会被放在 METALNESS 槽位
            if (ormMaps.empty()) {
                // 🌟 核心修复：即使从 METALNESS 槽位读取，也必须强行命名为 "texture_metallic"，以命中 Shader 逻辑！
                ormMaps = loadMaterialTextures(
                    material, aiTextureType_METALNESS, "texture_metallic");
            }

            if (!ormMaps.empty()) {
                // 找到了打包的 ORM 贴图，直接插入
                textures.insert(textures.end(), 
                    std::make_move_iterator(ormMaps.begin()), 
                    std::make_move_iterator(ormMaps.end()));
            } 
            else {
                // 🌟 极少数情况的兜底（比如 OBJ 格式）：只有真正不存在 ORM 时，才分开读取旧版独立贴图
                std::vector<Texture> roughnessMaps = loadMaterialTextures(
                    material, aiTextureType_DIFFUSE_ROUGHNESS, "texture_roughness");
                textures.insert(textures.end(), roughnessMaps.begin(), roughnessMaps.end());
            }
        }

        // 🌟 新增：判断逻辑
        bool isTransparent = false;
        bool isEmissive = false;
        bool isMasked = false;  // 🌟 新增
        std::string materialNameStr = "DefaultMaterial";

        // 🌟 初始化玻璃专属参数（合并原 ProcessMesh_Glass 逻辑）
        float glassAlpha = 0.35f;
        float glassRoughness = 0.08f;

        if(mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            
            // 1. 根据材质名称判断是否为半透明（玻璃等）
            aiString matName;
            material->Get(AI_MATKEY_NAME, matName);
            std::string nameStr = matName.C_Str();
            materialNameStr = nameStr; // 🌟 保存下来方便外部调试

            // 🌟🌟🌟 核心修改：全部转为小写，防止 UE 大小写作妖 🌟🌟🌟
            std::string lowerName = nameStr;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            // ==========================================
            // 🌟 材质精细分类逻辑：重排优先级！防止误杀 🌟
            // ==========================================

            // 修改 Model.h 里的材质拦截逻辑
            if (lowerName.find("glass") != std::string::npos ||
                lowerName.find("translucent") != std::string::npos)
            {
                // 只有纯玻璃是半透明
                isTransparent = true;
                isMasked = false;
            }
            else if (lowerName.find("windows_modular") != std::string::npos ||
                    lowerName.find("window") != std::string::npos)
            {
                // 🌟 整个窗户本体是 Masked (遮罩镂空)
                isMasked = true; 
                isTransparent = false;
            }
            // 第 2 步：假窗户 / 室内发光 (Opaque + Emissive)
            else if (lowerName.find("interior") != std::string::npos ||
                lowerName.find("fake") != std::string::npos ||
                lowerName.find("emissive") != std::string::npos)
            {
                isEmissive = true;
                isTransparent = false;
                isMasked = false;
            }
            // 第 3 步：窗框 / 铁丝网 (实体模型)
            else if (lowerName.find("frame") != std::string::npos ||
                    lowerName.find("fence") != std::string::npos)
            {
                isMasked = false;
                isTransparent = false;
            }

            // 🚀 第 4 步：【强力兜底】如果名字完全没匹配上，强制读取网格本身的透明度属性！
            if (!isTransparent) {
                float opacity = 1.0f;
                if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 0.99f) {
                    isTransparent = true;
                }
            }

            // 第 4 步：读取 glTF 官方的 AlphaMode (最权威兜底，防意外)
            aiString alphaMode;
            if (material->Get("$mat.gltf.alphaMode", 0, 0, alphaMode) == AI_SUCCESS) {
                std::string mode = alphaMode.C_Str();
                if (mode == "MASK")  { isMasked = true; isTransparent = false; }
                if (mode == "BLEND") { isTransparent = true; isMasked = false; }
                if (mode == "OPAQUE") { isTransparent = false; isMasked = false; }
            }
        }

        AABB meshAABB(minAABB, maxAABB);

        return Mesh(vertices, indices, textures, materialNameStr, meshAABB, isTransparent, isEmissive, isMasked, glassAlpha, glassRoughness);
    }

    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName)
    {
        std::vector<Texture> textures;
        // 准备一个存放“未来结果”的数组
        std::vector<std::future<TextureData>> futureTextures;

        // 【第一阶段：主线程派发任务】
        for(unsigned int i = 0; i < mat->GetTextureCount(type); i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);
            // 🌟 核心修复：解码 glTF 的 URI 格式，将 %20 变回真实的空格！
            std::string pathStr = urlDecode(str.C_Str());
            // std::string pathStr = str.C_Str();
            bool skip = false;
            
            // 1. 检查全局缓存，防止重复加载相同的贴图
            for(unsigned int j = 0; j < textures_loaded.size(); j++)
            {
                if(textures_loaded[j].path == pathStr)
                {
                    // 必须拷贝一份新的，并强行改写类型！绝对不能用以前的类型！
                    Texture cachedTex = textures_loaded[j];
                    cachedTex.type = typeName; 
                    
                    textures.push_back(cachedTex); 
                    skip = true; 
                    break;
                }
            }
            if(skip) continue;

            // 2. 拼接完整的硬盘路径
            std::string fullPath = directory + '/' + pathStr;

            // 🌟 核心魔法：开启后台线程去读硬盘和解压 PNG！
            // std::launch::async 会立刻在线程池里分配一个空闲核来执行这段 Lambda 代码
            futureTextures.push_back(std::async(std::launch::async, [fullPath, pathStr, typeName]() {
                TextureData data;
                data.path = pathStr;
                data.typeName = typeName;
                
                std::cout << "[后台线程] 正在解压贴图: " << fullPath << " ..." << std::endl;
                // 在后台极速解压，主线程完全不被阻塞！
                // 加上强力报错，别让它默默失败！
                data.pixels = stbi_load(fullPath.c_str(), &data.width, &data.height, &data.nrComponents, 4);
                if (!data.pixels) {
                    std::cout << "[ERROR] 贴图加载失败，文件不存在: " << fullPath << std::endl;
                }
                return data;
            }));
        }

        // 【第二阶段：主线程回收结果并生成 OpenGL 纹理】
        // ⚠️ 极其重要：OpenGL 的纹理生成 (glGenTextures) 绝对不能在子线程调用！必须回到主线程！
        for(auto& fut : futureTextures)
        {
            // fut.get() 会等待对应的子线程解压完毕，并拿出数据
            TextureData data = fut.get(); 

            Texture texture;
            texture.type = data.typeName;
            texture.path = data.path;

            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);

            if(data.pixels)
            {
                // 🌟 1. 区分内部格式：物理贴图用 RGBA，颜色贴图用 SRGB_ALPHA
                GLenum internalFormat = GL_RGBA;
                if (data.typeName == "texture_diffuse" || data.typeName == "texture_emissive") {
                    internalFormat = GL_SRGB_ALPHA; // 让硬件自动把 sRGB 贴图解码为线性空间
                }

                // 🌟 2. 源数据格式：永远是 GL_RGBA，绝不能是 GL_SRGB_ALPHA！
                GLenum dataFormat = GL_RGBA; 

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                // ⚠️ 注意看第 3 个参数和第 7 个参数！
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, data.width, data.height, 0, dataFormat, GL_UNSIGNED_BYTE, data.pixels);
                glGenerateMipmap(GL_TEXTURE_2D);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                stbi_image_free(data.pixels); // 释放内存
            }
            else
            {
                // R=128 (AO 0.5), G=128 (Roughness 0.5 - 避免除以0), B=255 (Metalness 1.0 或 法线 Z=1)
                unsigned char errorColor[] = { 128, 128, 255, 255 };
                std::cout << "[主线程] 警告: 贴图加载失败 -> " << data.path << std::endl;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, errorColor);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }

            textures.push_back(texture);
            textures_loaded.push_back(texture); // 存入缓存，下次别的网格要用就不用再读硬盘了
        }
        return textures;
    }
};

#endif