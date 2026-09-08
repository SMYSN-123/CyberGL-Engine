#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBiTangent;

// 传给片元着色器的变量
out vec4 currClipPos;
out vec4 prevClipPos;

out VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 TangentLightDir;
    mat3 TBN;
}vs_out;

layout (std140) uniform Matrices
{
    mat4 projection; // 槽位0：带抖动的矩阵！(给全场所有东西画图用)
    mat4 view;       // 槽位1：当前视图矩阵
    mat4 cleanProj;  // 槽位2：干净无抖动投影！(专门给 G-Buffer 算当前物理坐标用)
    mat4 prevProj;   // 槽位3：上一帧干净投影！(专门给 G-Buffer 算历史物理坐标用)
    mat4 prevView;   // 槽位4：上一帧视图矩阵
};

uniform mat4 model;
uniform mat3 NormalMatrix;

uniform vec3 lightDir;
uniform vec3 viewPos;

void main()
{
    // 🌟 正常的屏幕输出，必须使用你那个【带抖动的矩阵】
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // 算出当前帧和上一帧的裁剪空间坐标 (Clip Space)
    currClipPos = cleanProj * view * model * vec4(aPos, 1.0);
    prevClipPos = prevProj * prevView * model * vec4(aPos, 1.0);

    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
    vs_out.TexCoords = aTexCoords;

    // 1. 获取基础向量
    vec3 T = normalize(NormalMatrix * aTangent);
    vec3 B = normalize(NormalMatrix * aBiTangent); // 🌟 必须用到原始的副切线！
    vec3 N = normalize(NormalMatrix * aNormal);

    // 2. 施展 Gram-Schmidt 正交化，确保 T 绝对垂直于 N
    vec3 reorthogonalizedT = T - dot(T, N) * N;
    if (length(reorthogonalizedT) > 0.0001) 
    {
        T = normalize(reorthogonalizedT);
    } 
    else 
    {
        // 防崩溃抢救
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
    }

    // 3. 🌟🌟🌟 核心修复：还原 Assimp 算好的镜像手性 (Handedness) 🌟🌟🌟
    // 先计算标准的右手法线
    vec3 naturalB = cross(N, T); 
    // 判断 Assimp 传进来的原始 B 是顺着 naturalB 还是反着的？
    // 如果是反着的，说明这块多边形的 UV 是镜像过的，必须乘 -1！
    float handedness = dot(naturalB, B) < 0.0 ? -1.0 : 1.0; 
    
    // 生成最终的副切线
    B = naturalB * handedness; 

    // 4. 组装 TBN
    vs_out.TBN = mat3(T, B, N);

    mat3 invTBN = transpose(vs_out.TBN);

    vs_out.TangentViewPos  = invTBN * viewPos;
    vs_out.TangentFragPos  = invTBN * vs_out.FragPos;
    vs_out.TangentLightDir = invTBN * lightDir;
}