#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
// 如果你的 Model.h 支持法线贴图，这里应该还有 Tangent 和 Bitangent
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    mat3 TBN;
} vs_out;

uniform mat4 model;
uniform mat3 NormalMatrix;

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
    mat4 cleanProj;
    mat4 prevProj;
    mat4 prevView;
};

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    vs_out.FragPos = worldPos.xyz;
    vs_out.TexCoords = aTexCoords;
    
    vec3 N = normalize(NormalMatrix * aNormal);
    vec3 T = normalize(NormalMatrix * aTangent);
    vec3 B = cross(N, T);
    
    vs_out.Normal = N;
    vs_out.TBN = mat3(T, B, N);
    
    gl_Position = projection * view * worldPos;
}