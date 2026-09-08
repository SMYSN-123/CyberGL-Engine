#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 TexCoords;

// --- 🌟 核心：G-Buffer 纹理采样器 ---
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo_parallaxShadow;
uniform sampler2D gORM;

struct Light
{
    vec4 Position; // w 分量可以用来区分点光源（w=1）和聚光灯（w=0）
    vec4 Color;    // w 分量可以用来存储强度或其他参数
};

layout (std430, binding = 1) buffer LightBuffer
{
    Light lights[];
};

uniform int activePointLightsCount;

// 🟢 [新增] 引入模糊后的 SSAO 贴图
uniform sampler2D ssaoTexture;

// 🌧️ [新增] 全局湿度控制 (0.0 完全干燥 -> 1.0 暴雨积水)
uniform float u_GlobalWetness;
// 🌧️ [可选新增] 水坑遮罩纹理 (Puddle Mask)，用于让积水随机分布，而不是均匀一层
// uniform sampler2D puddleNoiseMap;

uniform float u_AmbientIntensity = 0.2; // 🌟 可调节的环境光强度 (0.0 ~ 1.0)

// --- 在光照着色器最前面加上调试拦截 ---
uniform bool u_DebugShowEmissive = false; // 这个值由 C++ 端控制传入

// --- 相机与矩阵 ---
layout (std140) uniform Matrices
{
    mat4 projection; // 槽位0：带抖动的矩阵！(给全场所有东西画图用)
    mat4 view;       // 槽位1：当前视图矩阵
    mat4 cleanProj;  // 槽位2：干净无抖动投影！(专门给 G-Buffer 算当前物理坐标用)
    mat4 prevProj;   // 槽位3：上一帧干净投影！(专门给 G-Buffer 算历史物理坐标用)
    mat4 prevView;   // 槽位4：上一帧视图矩阵
};

layout (std140) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[16];
};

// --- IBL 环境光贴图 ---
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D   brdfLUT;

// --- CSM 阴影专用 ---
uniform sampler2DArray shadowMap;
uniform float cascadePlaneDistances[16];
uniform int cascadeCount;

// --- 光照参数 (定向光) ---
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 lightColor;

const bool DEBUG_CSM_LAYER = false;
const float PI = 3.14159265359;

float D_GGX_TR(vec3 N, vec3 H, float roughness)
{
    float a2 = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k_dir = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k_dir) + k_dir;
    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ShadowCalculation(vec3 fragPosWorld, vec3 normal, out vec3 debugColor)
{
    vec4 fragPosViewSpace = view * vec4(fragPosWorld, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;
    for(int i = 0; i < cascadeCount; i++)
    {
        if(depthValue < cascadePlaneDistances[i])
        {
            layer = i;
            break;
        }
    }
    if(layer == -1) layer = cascadeCount - 1;

    if(layer == 0) debugColor = vec3(1, 0, 0);
    else if(layer == 1) debugColor = vec3(0, 1, 0);
    else if(layer == 2) debugColor = vec3(0, 0, 1);
    else debugColor = vec3(1, 1, 0);

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorld, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    float currentDepth = projCoords.z;
    if (currentDepth > 1.0) return 0.0;
    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x > 1.0 || projCoords.x < 0.0 || projCoords.y > 1.0 || projCoords.y < 0.0) return 0.0;

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    if (layer > 0) bias *= (layer + 1.0) * 0.5; 

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, float(layer))).r; 
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
    return shadow;
}

// ==========================================
// 主函数：优雅的数据解包与计算
// ==========================================
void main()
{
    // 🌟 第一步先拦截！
    if (u_DebugShowEmissive)
    {
        // 直接从 G-Buffer 中把 GBuffer 里的 Albedo 取出（此时存的是 debugEmissive 贴图数据）
        vec3 debugEmissive = texture(gAlbedo_parallaxShadow, TexCoords).rgb;
        
        // 直接输出给屏幕，跳过所有 PBR、点光源、CSM 阴影计算！
        // 如果外墙是黑色，那说明 100% 没绑贴图。
        // 如果外墙有颜色，那说明之前我们是把本来该发光的贴图弄丢了。
        FragColor = vec4(debugEmissive, 1.0); 
        return; 
    }

    // 🌟 1. 从 G-Buffer 中“解包”数据 (就像读取照片像素一样简单)
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 N       = texture(gNormal, TexCoords).rgb;

    float emissiveMask = texture(gNormal, TexCoords).a; // 🌟 拿回我们的 Mask！

    // 💡 优雅的小技巧：跳过背景(天空盒)的光照计算
    // 如果法线长度接近0，说明这里没有模型渲染过，直接丢弃或输出默认颜色
    if (length(N) < 0.1) discard; 

    vec4 albedo_shadow = texture(gAlbedo_parallaxShadow, TexCoords);
    vec3 albedo        = albedo_shadow.rgb;
    float parallaxShadow = albedo_shadow.a;

    vec4 orm_puddle = texture(gORM, TexCoords);

    float bakedAO  = orm_puddle.r;
    float ssao     = texture(ssaoTexture, TexCoords).r;
    float finalAO  = bakedAO * ssao;

    float roughness= max(orm_puddle.g, 0.04); // 避免纯粹的高光除零错误
    float metallic = clamp(orm_puddle.b, 0.0, 1.0);
    float puddleMask = orm_puddle.a; // 取出水坑掩码

    // 🌟 2. 准备 PBR 需要的向量 (全部都在世界空间下！)
    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 🌧️ 光学折射覆写：水是绝缘体，F0 约为 0.02
    F0 = mix(F0, vec3(0.02), puddleMask);

    // 🌟 3. 主平行光 (月光) 计算
    float attenuation = 1.0;
    vec3 radiance = lightColor * attenuation;

    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = D_GGX_TR(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = nominator / denominator;

    // 🌟🌟🌟 【新增这行神仙代码：物理高光限幅】 🌟🌟🌟
    // 强行压制 GGX 公式算出来的无限大高光！
    // 10.0 这个阈值你可以自己微调，越小闪烁越少，但水坑倒影会变暗。
    specular = min(specular, vec3(10.0));

    vec3 KS = F;
    vec3 KD = vec3(1.0) - KS;
    KD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (KD * albedo / PI + specular) * radiance * NdotL;

    // 计算阴影 (只对主平行光生效)
    vec3 debugCascadeColor = vec3(0.0);
    float shadow = ShadowCalculation(FragPos, N, debugCascadeColor);
    float visibility = (1.0 - shadow) * (1.0 - parallaxShadow);

    // 🌟 4. [核心新增] 点光源 (霓虹灯) 循环累加！
    vec3 Lo_PointLights = vec3(0.0);
    
    for(int i = 0; i < activePointLightsCount; ++i)
    {
        // 1. 解包数据
        vec3 lightPos = lights[i].Position.xyz;
        float lightRadius = lights[i].Position.w;   
        vec3 lightColor = lights[i].Color.xyz;
        float lightIntensity = lights[i].Color.w;

        // 🌟 核心修复：千万别提前 normalize！
        vec3 L_pt = lightPos - FragPos; // 真实的距离向量
        float distSquare = dot(L_pt, L_pt); // 真实的距离平方（比如离了10米，这里就是100）
        float dist = sqrt(distSquare); // 真实的距离

        L_pt = normalize(L_pt); // 算完距离后，再把它变成方向向量！
        vec3 H_pt = normalize(V + L_pt);

        // 🌟 UE4 物理平滑截断衰减 
        float distanceSq = max(distSquare, 0.0001);
        float attenuation = 1.0 / (distanceSq + 1.0); 

        float factor = distanceSq / (lightRadius * lightRadius);
        float smoothFactor = clamp(1.0 - factor * factor, 0.0, 1.0);
        float falloff = smoothFactor * smoothFactor;

        attenuation = attenuation * falloff * lightIntensity;

        if (attenuation <= 0.001) {
            continue;
        }

        vec3 radiance_pt = lightColor * attenuation;

        // PBR 计算
        vec3 F_pt  = fresnelSchlick(max(dot(H_pt, V), 0.0), F0);
        float NDF_pt = D_GGX_TR(N, H_pt, roughness);
        float G_pt   = GeometrySmith(N, V, L_pt, roughness);

        vec3 nominator_pt    = NDF_pt * G_pt * F_pt;
        float denominator_pt = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L_pt), 0.0) + 0.001;
        vec3 specular_pt     = nominator_pt / denominator_pt;

        // 🌟🌟🌟 【新增这行神仙代码：物理高光限幅】 🌟🌟🌟
        // 强行压制 GGX 公式算出来的无限大高光！
        // 10.0 这个阈值你可以自己微调，越小闪烁越少，但水坑倒影会变暗。
        specular_pt = min(specular_pt, vec3(10.0));

        vec3 KS_pt = F_pt;
        vec3 KD_pt = vec3(1.0) - KS_pt;
        KD_pt *= 1.0 - metallic;

        float NdotL_pt = max(dot(N, L_pt), 0.0);
        
        // 累加这盏灯的能量 (不乘 visibility！点光源不受平行光阴影影响)
        Lo_PointLights += (KD_pt * albedo / PI + specular_pt) * radiance_pt * NdotL_pt;
    }

    // 🌟 5. 间接光照 (IBL / 环境光)
    vec3 KS_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 KD_ambient = 1.0 - KS_ambient;
    KD_ambient *= 1.0 - metallic;

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse_ambient = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular_ambient = prefilteredColor * (KS_ambient * envBRDF.x + envBRDF.y);

    vec3 ambient = (KD_ambient * diffuse_ambient + specular_ambient) * finalAO;

    // // 🌟 赛博朋克光污染底光 (Fake GI)
    // float groundGlowFactor = max(0.0, dot(N, vec3(0.0, -1.0, 0.0))); 
    // vec3 cityGlow = albedo * vec3(0.8, 0.2, 0.9) * groundGlowFactor * 0.2;

    // vec3 nightTint = vec3(0.6, 0.7, 0.8);
    // float nightExposure = 0.05; 
    
    // vec3 finalAmbient = (ambient * nightExposure * nightTint) + cityGlow;

    // vec3 pbrColor = finalAmbient + (Lo * visibility) + Lo_PointLights;

    // 保持环境光纯净，只受 AO 影响
    vec3 finalAmbient = ambient * u_AmbientIntensity;

    // 最终 PBR 颜色（环境光 + 主光源 + 点光源）
    vec3 pbrColor = finalAmbient + (Lo * visibility) + Lo_PointLights;


    // =======================================================
    // 🌟🌟🌟 核心修改：寻找“黄金倍率”与色彩提纯 🌟🌟🌟
    // =======================================================
    
    // 1. 寻找黄金倍率：15.0f 显然太高了，引发了核爆。
    // 我们将其降至 5.0f (你可以根据喜好在 3.0 ~ 8.0 之间微调)
    float emissiveIntensity = 2.0f;
    
    // 2. 色彩提纯 (Vibrancy Boost)：
    // 原本的 Albedo 可能比较暗沉，我们用 pow 函数让霓虹灯的颜色变得更加鲜艳纯正！
    // vec3 neonColor = pow(albedo, vec3(0.8));
    // 🌟 终极纯净：直接使用 Albedo 作为霓虹灯颜色，不做任何抖动或偏移！
    vec3 neonColor = albedo;

    // 3. 计算最终的自发光能量
    vec3 emissiveLight = neonColor * emissiveMask * emissiveIntensity; 

    // 物理世界的光是线性叠加的：被照亮的漫反射 + 自己发出的光
    vec3 finalColor = pbrColor + emissiveLight;

    if(DEBUG_CSM_LAYER)
    {
        FragColor = vec4(pbrColor * 0.5 + debugCascadeColor * 0.5, 1.0);
    }
    else
    {
        FragColor = vec4(finalColor, 1.0);
    }
}