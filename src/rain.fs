#version 430 core
out vec4 FragColor;

in vec2 ParticleUV;
in float IsSplash;
in vec3 WorldPos;

struct Light { vec4 Position; vec4 Color; };
layout (std430, binding = 1) buffer LightBuffer { Light lights[]; };

uniform int activePointLightsCount;
uniform vec3 cameraPos;
uniform sampler2D gPositionMap; 
uniform vec2 screenSize;

void main()
{
    vec2 screenUV = gl_FragCoord.xy / screenSize;
    vec3 sceneWorldPos = texture(gPositionMap, screenUV).xyz;
    float sceneDepth = length(sceneWorldPos - cameraPos);
    float particleDepth = length(WorldPos - cameraPos);
    
    if (sceneDepth < particleDepth - 0.05) discard;
    float depthFade = clamp((sceneDepth - particleDepth) * 3.0, 0.0, 1.0);

    // 🌟 纯净的底色：水本身没有颜色，只有极微弱的环境反光
    vec3 rainColor = vec3(0.005, 0.008, 0.015);
    vec3 viewDir = normalize(cameraPos - WorldPos);

    for(int j = 0; j < activePointLightsCount; ++j) 
    {
        vec3 lightPos = lights[j].Position.xyz;
        float radius = lights[j].Position.w;
        float distToLight = distance(lightPos, WorldPos);

        if (distToLight < radius) 
        {
            float attenuation = 1.0 / (distToLight * distToLight + 1.0);
            float falloff = clamp(1.0 - pow(distToLight / radius, 4.0), 0.0, 1.0);
            
            // 简单的点高光，模拟水滴折射
            vec3 lightDir = normalize(lightPos - WorldPos);
            vec3 halfway = normalize(lightDir + viewDir);
            float highlight = pow(max(dot(-viewDir, halfway), 0.0), 64.0) * 3.0;
            
            // 🌟 增大霓虹灯对雨滴的染色强度
            rainColor += lights[j].Color.xyz * lights[j].Color.w * 0.25 * attenuation * falloff * (1.0 + highlight);
        }
    }

    // 计算遮罩形状
    float mask = 0.0;
    if (IsSplash < 0.5) // 雨丝
    {
        float dx = (ParticleUV.x - 0.5) * 2.0;
        float xFade = exp(-pow(dx * 4.5, 2.0));     // 边缘更柔和
        float yFade = pow(1.0 - ParticleUV.y, 3.0); // 头部亮，尾部淡
        mask = xFade * yFade;
    }
    else // 飞溅
    {
        float dx = (ParticleUV.x - 0.5) * 2.0;
        float xFade = exp(-pow(dx * 4.0, 2.0));
        float yFade = pow(1.0 - ParticleUV.y, 2.0);
        mask = xFade * yFade;
    }

    // 🌟 适度提高透明度，最高 0.4，并用 depthFade 柔化边缘
    float finalAlpha = mask * 0.4 * depthFade;
    if (finalAlpha < 0.01) discard;

    // 🌟 预乘 Alpha，亮度系数调到 3.0，让雨丝在霓虹灯下闪耀
    vec3 finalColor = rainColor * mask * 3.0;

    FragColor = vec4(finalColor, finalAlpha);
}