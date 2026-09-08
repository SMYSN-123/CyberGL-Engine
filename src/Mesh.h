#ifndef MESH_H
#define MESH_H

#include <vector>

#include "Shader.h"
#include "FrustumCulling.h" // 引入包围盒定义

struct Vertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;   // ✅ 新增：切线
    glm::vec3 Bitangent; // ✅ 新增：副切线
};

struct Texture
{
    unsigned int id;
    std::string type;
    std::string path;
};

class Mesh
{
public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    // 🌟 记录材质名称，方便后期调试和精确控制
    std::string materialName;

    // 🌟 新增：网格专属的包围盒
    AABB boundingBox;

    // 🌟 新增：材质分类标记
    bool isTransparent = false; 
    bool isEmissive = false;
    bool isMasked = false; // 🌟 新增：遮罩材质标记

    // 🌟 玻璃材质专属参数
    float glassAlpha = 0.35f;      // 基础透明度
    float glassRoughness = 0.08f;  // 极低粗糙度

    // 🌟 新增：接收从 Assimp 传来的 UV 缩放值
    glm::vec2 uvTiling = glm::vec2(1.0f, 1.0f);

    // 🌟 构造函数新增 materialName 参数
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures, std::string matName, AABB box, bool transparent = false, bool emissive = false, bool masked = false, float glassAlp = 0.35f, float glassRough = 0.08f, glm::vec2 tiling = glm::vec2(1.0f, 1.0f))
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;
        this->materialName = matName; // 保存材质名
        this->boundingBox = box;
        this->isTransparent = transparent; 
        this->isEmissive = emissive;
        this->isMasked = masked;
        this->glassAlpha = glassAlp;
        this->glassRoughness = glassRough;
        this->uvTiling = tiling; // 👈 新增赋值

        setupMesh();
    }

    void Draw(const Shader& shader)
    {
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;
        unsigned int reflectionNr = 1;
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);

            std::string number;
            std::string name = textures[i].type;

            if(name == "texture_diffuse")
                number = std::to_string(diffuseNr++);
            else if(name == "texture_specular")
                number = std::to_string(specularNr++);
            else if(name == "texture_reflection")
                number = std::to_string(reflectionNr++);

            shader.setInt(("material." + name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
        glActiveTexture(GL_TEXTURE0);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void DrawPBR(const Shader& shader) const
    {
        // =================================================================
        // 🌟🌟🌟 终极修复 0：物理清空纹理槽位，斩断“幽灵绑定”！🌟🌟🌟
        // 只要当前 Mesh 没有该贴图，这里绑定的 0 就会告诉 OpenGL 和 RenderDoc：这个槽位是空的！
        // =================================================================
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);

        // =================================================================
        // 🌟🌟🌟 核心修复 1：清空 Shader 状态
        // =================================================================
        shader.setBool("useAlbedoMap", false);
        shader.setBool("useNormalMap", false);
        shader.setBool("usePackedMap", false);   
        shader.setBool("useEmissiveMap", false);
        shader.setBool("useMetalMap", false);
        shader.setBool("useRoughnessMap", false);
        shader.setBool("useAOMap", false);

        // 设置安全的默认值 (防止没有贴图的物体变成死黑或无限反光)
        shader.setVec3("albedoValue", glm::vec3(0.5f));
        shader.setFloat("metalValue", 0.0f);     // 👈 核心修复 2：名字必须是 metalValue
        shader.setFloat("roughnessValue", isTransparent ? glassRoughness : 0.65f);
        shader.setFloat("aoValue", 1.0f);        // 👈 核心修复 3：默认 AO 必须是 1.0 (白)，不能是 0！
        
        bool hasDiffuse = false;
        bool hasNormal = false;
        bool hasEmissive = false;
        bool hasORM = false;      
        
        // 1. 🌟 预先定死所有贴图的采样器槽位 (Slot)，杜绝互相顶替！
        shader.setInt("albedoMap", 0);
        shader.setInt("normalMap", 1);
        shader.setInt("metallicMap", 2);
        shader.setInt("emissiveMap", 3);

        // 2. 🌟 遍历贴图时，不再使用 + i，而是对号入座绑定到指定的 GL_TEXTUREX
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            std::string name = textures[i].type;

            if(name == "texture_diffuse") {
                glActiveTexture(GL_TEXTURE0); // 👈 定死在 0 号槽
                shader.setBool("useAlbedoMap", true);
                glBindTexture(GL_TEXTURE_2D, textures[i].id);
                hasDiffuse = true;
            }
            else if(name == "texture_normal" || name == "texture_height") {
                glActiveTexture(GL_TEXTURE1); // 👈 定死在 1 号槽
                shader.setBool("useNormalMap", true);
                glBindTexture(GL_TEXTURE_2D, textures[i].id);
                hasNormal = true;
            }
            else if (name == "texture_orm" || name == "texture_metalness" || name == "texture_metallic" || name == "texture_roughness") {
                glActiveTexture(GL_TEXTURE2); // 👈 定死在 2 号槽
                shader.setBool("usePackedMap", true);
                glBindTexture(GL_TEXTURE_2D, textures[i].id);
                hasORM = true;
            }
            else if(name == "texture_emissive") { 
                glActiveTexture(GL_TEXTURE3); // 👈 定死在 3 号槽
                shader.setBool("useEmissiveMap", true);
                glBindTexture(GL_TEXTURE_2D, textures[i].id);
                hasEmissive = true;
            }
        }
        
        shader.setBool("isGlassMaterial", isTransparent);
        shader.setFloat("glassAlpha", glassAlpha);
        shader.setFloat("glassRoughness", glassRoughness);
        
        // 我们已经在顶部设置了默认值，所以这里的判定只需要关注业务逻辑
        // (旧代码这里的 uniform 名字全部写错了，已经被我在顶部修复)

        // 🌟 核心修复：使用模型读取的真实 Tiling，不再写死 1.0f
        shader.setVec2("uvTiling", this->uvTiling);
        shader.setBool("isMasked", isMasked);
        shader.setFloat("alphaCutoff", 0.5f);
        
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

    // 同时，你还需要一个支持实例化的绘制函数
    void DrawInstanced(Shader& shader, unsigned int amount)
    {
        // 绑定纹理部分 (直接复制 Draw 函数里的纹理绑定代码过来)
        // ... (省略纹理绑定代码，和 Draw 一模一样) ...
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;
        unsigned int reflectionNr = 1;
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            std::string number;
            std::string name = textures[i].type;
            if(name == "texture_diffuse") number = std::to_string(diffuseNr++);
            else if(name == "texture_specular") number = std::to_string(specularNr++);
            else if(name == "texture_reflection") number = std::to_string(reflectionNr++);

            shader.setInt(("material." + name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
        glActiveTexture(GL_TEXTURE0);

        // 绘制
        glBindVertexArray(VAO);
        // ✅ 关键：使用 Instanced 版本
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0, amount);
        glBindVertexArray(0);
    }

    // 专门为这个 Mesh 配置实例化矩阵属性 (Location 5, 6, 7, 8)
    void SetupInstancedAttributes(unsigned int instanceVBO)
    {
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

        // mat4 占用 4 个 vec4 插槽
        size_t vec4Size = sizeof(glm::vec4);

        // ✅ 关键修复：显式将其转换为 GLsizei (32位)，消除“可能丢失数据”的警告
        // 因为我们知道 stride (64字节) 肯定放得进 32位整数里
        GLsizei stride = static_cast<GLsizei>(4 * vec4Size);

        // Loc 5
        glEnableVertexAttribArray(5); 
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
        // Loc 6
        glEnableVertexAttribArray(6); 
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)(1 * vec4Size));
        // Loc 7
        glEnableVertexAttribArray(7); 
        glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * vec4Size));
        // Loc 8
        glEnableVertexAttribArray(8); 
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * vec4Size));

        // 设置除数 (实例化关键)
        glVertexAttribDivisor(5, 1);
        glVertexAttribDivisor(6, 1);
        glVertexAttribDivisor(7, 1);    
        glVertexAttribDivisor(8, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // 🌟 [新增] 纯净版绘制：只负责画几何体，不碰任何贴图！
    void DrawGeometryOnly() const
    {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
private:
    unsigned int VAO, VBO, EBO;

    void setupMesh()
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(2);

        // ✅ 3: 切线 (Tangent)
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        glEnableVertexAttribArray(3);
        // ✅ 4: 副切线 (Bitangent)
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
        glEnableVertexAttribArray(4);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
};

#endif