#version 430 core

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    mat3 TBN;
} fs_in;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D emissiveMap;

uniform bool useAlbedoMap = false;
uniform bool useNormalMap = false;
uniform bool usePackedMap = false;
uniform bool useEmissiveMap = false;

uniform vec3 albedoValue = vec3(0.5);
uniform float metalValue = 0.0;
uniform float roughnessValue = 0.65;

// 🌟 玻璃专属参数 (与 C++ 严格对应)
uniform float glassAlpha = 0.35;
uniform bool isGlassMaterial = false;

uniform vec3 viewPos;
const float PI = 3.14159265359;

out vec4 FragColor;

// ================================================
// 🌟 关键函数 1：获取最终的 Alpha 值
// ================================================
float GetFinalAlpha(float baseAlpha)
{
    if (!isGlassMaterial) {
        return baseAlpha;
    }
    
    // 对于玻璃：如果 BaseColor.Alpha == 1.0，使用 C++ 传入的值
    if (baseAlpha > 0.99) {
        return glassAlpha;
    }
    
    // 否则使用贴图中的 Alpha，但要确保不要太低
    return max(baseAlpha, glassAlpha * 0.5);
}

// ================================================
// 🌟 关键函数 2：根据高光强度调节 Alpha
// ================================================
float AdjustAlphaForSpecular(float baseAlpha, vec3 specularColor)
{
    if (!isGlassMaterial) {
        return baseAlpha;
    }
    
    // 计算高光强度
    float specIntensity = max(max(specularColor.r, specularColor.g), specularColor.b);
    
    // 高光强的地方提高不透明度，防止反光被吃掉
    // 这是关键：高光本身就代表不透明
    return clamp(baseAlpha + specIntensity * 0.4, 0.0, 1.0);
}

// PBR 相关函数...（保持原来的）

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    a = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a;
    float denom = (NdotH2 * (a - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

void main()
{
    vec4 albedoAlpha = useAlbedoMap ? texture(albedoMap, fs_in.TexCoords) : vec4(albedoValue, 1.0);
    
    // 🚀 [修复 1]：既然 C++ 用了 sRGB，这里绝对不能再 pow(2.2)！直接拿 rgb！
    vec3 albedo = albedoAlpha.rgb;
    
    // 获取玻璃 Alpha
    float alpha = GetFinalAlpha(useAlbedoMap ? albedoAlpha.a : 1.0);
    
    float metallic = metalValue;
    float roughness = roughnessValue;
    
    if (usePackedMap) {
        vec3 orm = texture(metallicMap, fs_in.TexCoords).rgb;
        roughness = orm.g;
        metallic = orm.b;
    }
    
    if (isGlassMaterial && roughness > 0.2) {
        roughness = roughnessValue;  // 玻璃极低粗糙度
    }
    roughness = clamp(roughness, 0.02, 1.0);
    
    vec3 N = normalize(fs_in.Normal);
    if (useNormalMap) {
        vec3 normal = texture(normalMap, fs_in.TexCoords).rgb;
        normal = normal * 2.0 - 1.0;
        N = normalize(fs_in.TBN * normal);
    }
    
    vec3 V = normalize(viewPos - fs_in.FragPos);
    
    vec3 F0 = vec3(0.04);
    if (!isGlassMaterial) {
        F0 = mix(F0, albedo, metallic);
    }
    
    vec3 L = normalize(vec3(1.0, 2.0, 1.0));
    vec3 H = normalize(V + L);
    
    float distance = length(vec3(1.0, 2.0, 1.0) - fs_in.FragPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = vec3(1.0) * attenuation * 2.0; // 这里的亮度也可以调暗点配合赛博之夜
    
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    if (isGlassMaterial) {
        kD = vec3(0.0);  
    } else {
        kD *= 1.0 - metallic;
    }
    
    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    
    vec3 ambient = vec3(0.05) * albedo;
    
    vec3 emissive = vec3(0.0);
    if (useEmissiveMap) {
        emissive = texture(emissiveMap, fs_in.TexCoords).rgb;
    }
    
    vec3 specularTerm = specular * radiance * NdotL;
    alpha = AdjustAlphaForSpecular(alpha, specularTerm);

    // 🚀 [玻璃质感/存在感强化]：菲涅尔边缘不透明度效应
    if (isGlassMaterial) {
        float viewDot = max(dot(N, V), 0.0);
        // 当视线越贴近边缘，fresnelAlpha 越接近 1.0
        float fresnelAlpha = pow(1.0 - viewDot, 3.0); 
        
        // 边缘强制变得不透明且反光，这是 UE 玻璃看起来明显的关键！
        alpha = mix(alpha, 0.85, fresnelAlpha); 
        
        // 玻璃缺少环境反射，强行放大高光项来模拟玻璃质感
        Lo += specularTerm * 2.0; 
    }

    
    // 物理合并
    vec3 color = ambient + Lo + emissive;
    
    // 🚀 [修复 2]：彻底删掉这里的 ToneMapping 和 Gamma Correction！
    // 因为这是在画进 HDR 缓冲！必须留给后期的 PostProcess 去做！
    
    FragColor = vec4(color, alpha);
}