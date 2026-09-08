#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D ssrTraceTexture;  // RG: Reflected UV, B: Visibility
uniform sampler2D hdrColorCopyTexture;  // 备份的 HDR 原图 (自带 Mipmap)
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gORM;
uniform sampler2D gAlbedo_parallaxShadow; // 额外传入 Albedo 和 Parallax Shadow 数据

layout (std140) uniform Matrices
{
    mat4 projection; // 槽位0：带抖动的矩阵！(给全场所有东西画图用)
    mat4 view;       // 槽位1：当前视图矩阵
    mat4 cleanProj;  // 槽位2：干净无抖动投影！(专门给 G-Buffer 算当前物理坐标用)
    mat4 prevProj;   // 槽位3：上一帧干净投影！(专门给 G-Buffer 算历史物理坐标用)
    mat4 prevView;   // 槽位4：上一帧视图矩阵
};

// PBR 菲涅尔方程
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    // 1. 读取基础数据
    vec3 originalColor = texture(hdrColorCopyTexture, TexCoords).rgb;

    // 🌟 【修改】我们不再只采样一次 SSR 数据，而是做一个 3x3 的盒型模糊 (Box Blur)
    vec2 texelSize = 1.0 / textureSize(ssrTraceTexture, 0); // 获取一个像素的大小
    vec4 ssrData = vec4(0.0);
    
    // 收集周围 9 个像素的数据并求和
    for(int x = -1; x <= 1; ++x) 
    {
        for(int y = -1; y <= 1; ++y) 
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            ssrData += texture(ssrTraceTexture, TexCoords + offset);
        }
    }
    // 取平均值！噪点被抹平了
    ssrData /= 9.0;

    vec2 refUV = ssrData.xy;
    float visibility = ssrData.b;

    // 如果完全没命中，直接退回原图，省性能
    if (visibility <= 0.01) { 
        FragColor = vec4(originalColor, 1.0);
        return;
    }

    vec3 worldNormal = texture(gNormal, TexCoords).rgb;
    vec3 worldPos = texture(gPosition, TexCoords).rgb;
    vec4 orm = texture(gORM, TexCoords);
    float roughness = orm.g;
    float metallic = orm.b;
    float puddleMask = orm.a;

    mat4 invView = inverse(view);
    vec3 cameraPos = vec3(invView[3]); 
    vec3 V = normalize(cameraPos - worldPos);
    vec3 N = normalize(worldNormal);

    // 2. 获取 SSR 的物理模糊反射色
    float MAX_REFLECTION_LOD = 6.0;
    float lodLevel = roughness * MAX_REFLECTION_LOD;
    vec3 reflectedColor = textureLod(hdrColorCopyTexture, refUV, lodLevel).rgb;

    // 3. 计算微表面菲涅尔 (Fresnel)
    vec3 albedo = texture(gAlbedo_parallaxShadow, TexCoords).rgb;
    vec3 F0 = mix(vec3(0.04), albedo, metallic); 
    F0 = mix(F0, vec3(0.02), puddleMask); // 积水区域的 F0
    
    // Schlick 近似计算当前视角的反射强度
    vec3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);

    // 🌟 赛博朋克专属作弊：强行放大积水区域的反射强度
    if (puddleMask > 0.1) {
        // 让水坑的反射更具侵略性，即使是俯视也能看到明显的霓虹灯
        F = clamp(F * 3.0 + vec3(0.1), 0.0, 1.0); 
    }

    float validSSRWeight = visibility * smoothstep(0.8, 0.1, roughness); // 放宽粗糙度容忍

    // 🌟 物理覆盖与自发光增强
    vec3 finalColor = originalColor;
    if (validSSRWeight > 0.0) 
    {
        // 反射光乘以 F 后，作为增量直接叠加，这能让霓虹灯的倒影亮得刺眼 (Bloom 会更漂亮)
        // 降低底色(albedo)在强反射下的权重，实现"镜面"替换感
        vec3 mixTarget = originalColor * (1.0 - F * validSSRWeight) + reflectedColor * F;
        
        // 额外给反射加点料，让赛博朋克高对比度更明显
        finalColor = mix(originalColor, mixTarget, validSSRWeight * 1.5); 
    }

    // 暴力输出！
    FragColor = vec4(finalColor, 1.0);
}