#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec4 gNormal; // 🌟 将正常的 normal 放 xyz，把白嫖来的自发光 mask 放进 w 通道！
layout (location = 2) out vec4 gAlbedo_parallaxShadow;
layout (location = 3) out vec4 gORM; // 借用 Alpha 通道传递水坑遮罩！
layout (location = 4) out vec2 gVelocity;

in vec4 currClipPos;
in vec4 prevClipPos;

in VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 TangentLightDir;
    mat3 TBN;
}fs_in;

// --- 纹理全家桶 ---
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D depthMap;
uniform sampler2D metallicMap; // 如果是三合一，这其实是 ORM 贴图！
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;
// 🌟🌟🌟 新增：自发光贴图接收槽位！
uniform sampler2D emissiveMap;

// 其他配置
uniform float height_scale;
uniform bool useParallax;
uniform bool usePackedMap;

// --- 🌟 材质状态开关与纯数字接收 ---
uniform bool useAlbedoMap;
uniform vec3 albedoValue;

uniform bool useNormalMap;

uniform bool useMetalMap;    
uniform float metalValue;    

uniform bool useRoughnessMap;
uniform float roughnessValue;

uniform bool useAOMap;
uniform float aoValue;

// 🌟🌟🌟 新增：自发光开关！
uniform bool useEmissiveMap;

// 🌧️ [新增] 全局湿度控制与噪音贴图，从光照阶段搬移到这里
uniform float u_GlobalWetness;
// uniform sampler2D puddleNoiseMap;

uniform float u_Time; // C++ 传进来的运行时间 glfwGetTime()

// --- 新增：UV 和 Emissive 排查调试开关 ---
uniform bool u_DebugEmissiveUV = false; // C++ 端传 false/true 进来

uniform vec2 uvTiling;

uniform bool isMasked;
uniform float alphaCutoff;

// ==========================================
// 🌟 黑魔法 1：3D 向量转 2D 的时空哈希函数
// ==========================================
vec2 hash32(vec3 p3) {
    p3 = fract(p3 * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// --- 核心：程序化雨滴涟漪生成器 ---
// 输入：世界坐标的 XZ 平面 (uv), 缩放比例 (scale), 系统时间 (time)
// 输出：一个被扰动的法线向量 (Normal)
// ==========================================
// 🌟 黑魔法 2：真正的多层级随机动态涟漪
// ==========================================
vec3 ComputeRipples(vec2 uv, float scale, float time) {
    vec3 normalSum = vec3(0.0);
    float weightSum = 0.0;
    
    // 我们叠加两层波纹，让水面看起来更加错落有致
    for (float layer = 0.0; layer < 2.0; layer++) {
        // 让不同层的网格稍微错开
        vec2 p = uv * scale + vec2(layer * 12.34);
        vec2 i = floor(p);
        vec2 f = fract(p);
        
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vec2 neighbor = vec2(x, y);
                
                // 🌟 1. 彻底抛弃 floor(time)！种子只受空间位置和所在层数影响
                vec2 dropPos = hash32(vec3(i + neighbor, layer)); 
                vec2 dropCenter = neighbor + dropPos;
                
                vec2 diff = f - dropCenter;
                float dist = length(diff);
                
                // 🌟 2. 使用连续的时间 fract，利用 dropPos.x 作为相位偏移，让每滴雨下落时间不同步
                float dropTime = fract(time * 1.5 + dropPos.x);
                
                float wave = sin(dist * 30.0 - dropTime * 20.0);
                
                // 🌟 3. 完美的生命周期包络线 (Envelope)
                // 刚出生(0.0)时迅速变亮，到达 0.6 时就开始衰减，到 1.0 时【绝对】透明！
                // 这样当 fract 重新跳回 0.0 时，波纹刚好是看不见的，完美掩盖了跳变残影！
                float timeFade = smoothstep(0.0, 0.1, dropTime) * smoothstep(1.0, 0.6, dropTime);
                float distFade = smoothstep(0.5, 0.1, dist);
                
                float fade = distFade * timeFade;
                
                vec2 normalOffset = -diff * wave * fade;
                normalSum += vec3(normalOffset.x, fade, normalOffset.y);
                weightSum += fade;
            }
        }
    }
    if (weightSum > 0.0) return normalize(vec3(normalSum.x * 2.0, 1.0, normalSum.z * 2.0));
    return vec3(0.0, 1.0, 0.0); 
}

vec2 parallaxMapping(vec2 texCoords, vec3 viewDir, vec3 lightDir, out float parallaxShadow)
{
    // ✅ 迭代法实现视差映射
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDir))); // 根据视角动态调整层数

    float LayerDepth = 1.0 / numLayers; // 每层的深度
    float currentLayerDepth = 0.0; // 当前层的深度

    vec2 p = viewDir.xy / max(viewDir.z, 0.1); // 先算标准偏移
    p = p * height_scale;                      // 乘上高度缩放

    // 🔥 关键防暴走逻辑：如果偏移量太长，强制截断 🔥
    float maxOffset = 0.05; // 限制最大偏移量 (根据实际效果微调，0.05-0.1 比较合适)
    if (length(p) > maxOffset)
    {
        p = normalize(p) * maxOffset;
    }

    vec2 deltaTexCoords = p / numLayers; // 每层的纹理坐标增量

    vec2 currentTexCoords = texCoords; // 当前层的纹理坐标
    float currentDepthMapValue = texture(depthMap, currentTexCoords).r; // 当前层的深度贴图值

    // while(currentLayerDepth < currentDepthMapValue)
    // {
    //     currentTexCoords -= deltaTexCoords; // 移动到下一层
    //     currentLayerDepth += LayerDepth; // 增加当前层的深度
    //     currentDepthMapValue = texture(depthMap, currentTexCoords).r; // 获取新层的深度贴图值
    // }
    // 🛡️ 安全锁：使用 for 循环代替 while，防止显卡未响应
    for(int i = 0; i < 40; ++i) 
    {
        if(currentLayerDepth >= currentDepthMapValue) 
            break; // 找到深度层了，退出

        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = texture(depthMap, currentTexCoords).r;  
        currentLayerDepth += LayerDepth;  
    }
    // return currentTexCoords; // 返回最终的纹理坐标

    vec2 prevTexCoords = currentTexCoords + deltaTexCoords; // 上一层的纹理坐标

    float afterDepth = currentDepthMapValue - currentLayerDepth; // 当前层的深度差
    float beforeDepth = texture(depthMap, prevTexCoords).r - (currentLayerDepth - LayerDepth); // 上一层的深度差

    float weight = afterDepth / (afterDepth - beforeDepth); // 线性插值权重

    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight); // 线性插值计算最终纹理坐标

    // return finalTexCoords; // 返回最终的纹理坐标

    // ✅ 新增 - 视差自阴影 (找光线遮挡)
    vec2 P_Light = lightDir.xy / max(lightDir.z, 0.1) * height_scale; // 光线方向的纹理坐标偏移
    vec2 deltaTexCoordsLight = P_Light / numLayers; // 每层的光线纹理坐标增量

    float currentLayerDepthLight = texture(depthMap, finalTexCoords).r; // 最终层的深度贴图值
    vec2 currentTexCoordsLight = finalTexCoords; // 最终层的纹理坐标

    float shadowAccumulation = 0.0; // 积累阴影
    int shadowSamples = 0; // 采样计数

    // while(currentLayerDepthLight > 0.0)
    // {
    //     currentTexCoordsLight += deltaTexCoordsLight; // 沿光线方向移动
    //     currentLayerDepthLight -= LayerDepth; // 增加当前层的深度

    //     float currentDepthMapValueLight = texture(depthMap, currentTexCoordsLight).r; // 获取新层的深度贴图值

    //     // 判定：如果【光线现在的深度】 > 【当前地形深度】
    //     // 说明光线还在地形下面（被挡住了！）
    //     if(currentLayerDepthLight > currentDepthMapValueLight)
    //     {
    //         // 被挡住了！累加阴影
    //         // 柔和阴影：累加权重
    //         shadowAccumulation += 1.0;
    //     }
    //     shadowSamples++;
    // }
    // 🛡️ 安全锁：for 循环
        for(int i = 0; i < 40; ++i)
        {
            // 往上走，直到走出表面 (depth <= 0)
            if(currentLayerDepthLight <= 0.0) 
                break;

            currentLayerDepthLight -= LayerDepth;
            currentTexCoordsLight += deltaTexCoordsLight;
            float currentDepthMapValueLight = texture(depthMap, currentTexCoordsLight).r;

            // 如果现在的光线高度 < 地形高度，说明被挡住了
            if(currentLayerDepthLight > currentDepthMapValueLight)
            {
                // 简单的软阴影累加
                shadowAccumulation += 1.0; 
            }
            shadowSamples++;
        }

    // 计算最终阴影系数 (0.0 = 全黑, 1.0 = 全亮)
    // 如果没有样本被挡住，结果是 1.0
    // 如果有一半被挡住，结果是 0.5 (模拟软阴影)
    if(shadowSamples > 0)
        parallaxShadow = shadowAccumulation / float(shadowSamples); // 计算平均阴影
    else
        parallaxShadow = 0.0;

    return finalTexCoords; // 返回最终的纹理坐标
}

void main()
{
    // 🌟 1. 调试拦截段
    if (u_DebugEmissiveUV)
    {
        // 情况A：查看真实的 Emissive 贴图长什么样，有没有正常采样
        vec3 debugEmissive = texture(emissiveMap, fs_in.TexCoords).rgb;
        // 情况B：看看这个模型的 UV 到底长什么样，是不是挤在了一起或飞出去了
        vec2 debugUV = fs_in.TexCoords; 

        // 将 Emissive 输出给 G-Buffer 的 Albedo 槽位，把 UV 输出给法线槽位看看
        // 注意：因为图里外墙没发光，如果你看到这行输出是纯黑(0,0,0)，说明贴图没绑上或者UV没指对！
        gAlbedo_parallaxShadow = vec4(debugEmissive, 1.0); 
        gNormal = vec4(vec3(debugUV, 0.0), 1.0); // 用颜色表示 UV，红绿通道即 UV 坐标
        gPosition = vec3(0.0); // 跳过世界坐标
        gORM = vec4(0.0);
        gVelocity = vec2(0.0);
        return; // 直接结束，不跑下面的 PBR 逻辑！
    }

    vec2 texCoords = fs_in.TexCoords * uvTiling;

    vec3 viewDir_Tangent = normalize(fs_in.TangentViewPos - fs_in.TangentFragPos);
    vec3 lightDir_Tangent = normalize(fs_in.TangentLightDir);

    // 🌟 核心：透视除法 (Perspective Divide)
    // 为什么在这里除？因为从 Vertex 到 Fragment 发生的光栅化插值，只有对除以 w 之后的值做插值才是透视正确的！
    vec2 currNDC = currClipPos.xy / currClipPos.w;
    vec2 prevNDC = prevClipPos.xy / prevClipPos.w;

    // NDC 的范围是 [-1, 1]。有些 TAA 实现习惯把它映射到 [0, 1] 纹理坐标系。
    // 我们这里为了方便，先把 NDC 映射到 [0, 1] 的 UV 坐标。
    vec2 currUV = currNDC * 0.5 + 0.5;
    vec2 prevUV = prevNDC * 0.5 + 0.5;

    // 当前 UV 减去 历史 UV = 物体在屏幕上滑动的速度！
    gVelocity = currUV - prevUV; 

    float parallaxShadow = 0.0;
    
    if (useParallax)
    {
        texCoords = parallaxMapping(texCoords, viewDir_Tangent, lightDir_Tangent, parallaxShadow);
    }

    // =======================================================================
    // 🌟🌟🌟 终极修复：Alpha Test (遮罩裁剪) 🌟🌟🌟
    // =======================================================================
    vec4 albedoTex = useAlbedoMap ? texture(albedoMap, texCoords) : vec4(albedoValue, 1.0);

    // // 🔪 真正的裁剪在这里！如果开启了遮罩且 Alpha 太低，直接丢弃像素！
    // if (isMasked && albedoTex.a < alphaCutoff) {
    //     discard;
    // }

    vec3 albedo = albedoTex.rgb;

    // =======================================================================
    // 🌟🌟🌟 真·工业级：只相信真实的 Emissive Map 🌟🌟🌟
    // =======================================================================
    float emissiveMask = 0.0;
    vec3 emissiveColor = vec3(0.0);

    if (useEmissiveMap) 
    {
        emissiveColor = texture(emissiveMap, texCoords).rgb;
        
        // 使用更符合人眼感知的亮度公式 (Luminance) 提取 Mask，比 max 更精准
        emissiveMask = dot(emissiveColor, vec3(0.299, 0.587, 0.114));

        // 🌟 核心修复 1：用 mix 直接替换底色，或者用 += 相加，绝对不要用 max()！
        if (emissiveMask > 0.0) 
        {
            // 方案 A：直接相加 (适用于发光强度较高的 HDR 工作流)
            // albedo += emissiveColor; 
            
            // 方案 B：平滑覆盖 (如果发光贴图只是普通的 LDR 颜色，推荐这种，防止颜色过曝变白)
            float blendFactor = smoothstep(0.0, 0.1, emissiveMask);
            albedo = mix(albedo, emissiveColor, blendFactor);
        }
    }

    // 2. 获取法线 (Normal)
    vec3 normal;
    if (useNormalMap)
    {
        vec3 normalMapValue = texture(normalMap, texCoords).rgb;
        normalMapValue = normalMapValue * 2.0 - 1.0;
        normalMapValue.y = -normalMapValue.y;
        
        // --- 🛡️ 查验与矫正逻辑开始 ---
        
        // 1. 强行重新正交化 TBN 矩阵（防止 VS 传过来的 TBN 被非等比缩放拉扯变形）
        vec3 N = normalize(fs_in.TBN[2]);
        vec3 T = normalize(fs_in.TBN[0]);
        // 施展 Gram-Schmidt 正交化魔法，强行让 T 垂直于 N
        T = normalize(T - dot(T, N) * N);
        // 重新计算副切线 B，确保绝对的右手/左手坐标系完美垂直
        vec3 B = cross(N, T); 
        mat3 perfectTBN = mat3(T, B, N);

        // 2. 应用完美的 TBN
        normal = normalize(perfectTBN * normalMapValue);
        
        // --- 🛡️ 查验与矫正逻辑结束 ---
    } 
    else 
    {
        normal = normalize(fs_in.TBN[2]);
    }

    // 3. 获取 PBR (ORM)
    float metallic, roughness, ao;
    if (usePackedMap)
    {
        // 完美适配 UE5 ORM: R=AO, G=Roughness, B=Metallic
        vec3 orm = texture(metallicMap, texCoords).rgb;
        ao        = max(orm.r, 0.05); // 👈 加个 max()，就算 UE 给的是黑图，也不至于让阴影死黑！
        roughness = orm.g;
        metallic  = orm.b;
    }
    else
    {
        metallic  = useMetalMap     ? texture(metallicMap, texCoords).r  : metalValue;
        roughness = useRoughnessMap ? texture(roughnessMap, texCoords).r : roughnessValue;
        ao        = useAOMap        ? texture(aoMap, texCoords).r        : aoValue;
    }

    // ==========================================
    // 🌧️ 1. 全局物理湿滑处理 (Global PBR Wetness)
    // ==========================================
    vec3 geoNormal = normalize(fs_in.TBN[2]);
    
    // 改成这样：只有朝上的面 (Y > 0) 才会淋湿，垂直的墙面 (Y = 0) 完全不积水
    float upFactor = clamp(geoNormal.y, 0.0, 1.0);
    
    // 生成一个微观的表面孔隙噪声，让湿润感不要太平滑死板
    float porousNoise = fract(sin(dot(fs_in.FragPos.xz, vec2(12.9898, 78.233))) * 43758.5453);
    
    // 最终湿度：受全局降雨量、朝向和表面孔隙率共同影响
    float wetLevel = u_GlobalWetness * upFactor * mix(0.7, 1.0, porousNoise);

    // 🌟 物理覆写 A：变暗 (内部散射吸收)
    // 注意：纯金属不吸水（比如铁桶），只有绝缘体（如石头、木头）才会显著变暗！
    float darkeningFactor = mix(0.3, 1.0, metallic); // 非金属变暗到 30%，金属保持 100%

    // 🌟 物理覆写防呆：招牌作为发光体，绝不能被雨水打湿变黑！
    // 如果 emissiveMask 很高，那么 finalDarkening 就接近 1.0，免疫雨水变暗效应！
    float finalDarkening = mix(darkeningFactor, 1.0, emissiveMask);

    albedo = mix(albedo, albedo * finalDarkening, wetLevel);

    // albedo = vec3(albedoTex.a, albedoTex.a, albedoTex.a);

    // 🌟 物理覆写 B：变滑 (微表面被水填平)
    // 即使是墙壁，湿了也会泛现出一点点镜面高光
    // 🌟 修正1：让非水坑的潮湿地面也变得很滑 (Roughness 降到 0.15 左右)
    roughness = mix(roughness, clamp(roughness * 0.2, 0.05, 0.3), wetLevel);

    // ==========================================
    // 🌧️ 工业级程序化水坑系统 (Organic Procedural Puddles)
    // ==========================================
    // 严格限制：只在绝对平坦的地面生成水坑 (Y > 0.9)
    float isGround = smoothstep(0.9, 0.95, geoNormal.y);

    if (isGround > 0.0) 
    {
        vec2 pos = fs_in.FragPos.xz;
        float noise = sin(pos.x * 0.4) * cos(pos.y * 0.4) * 0.5 + 0.5; 
        noise += sin(pos.x * 1.5 + 1.0) * cos(pos.y * 1.2 - 0.5) * 0.25; 
        noise += sin(pos.x * 3.0 + 2.0) * cos(pos.y * 3.0 + 1.0) * 0.125; 
        noise = clamp(noise, 0.0, 1.0);

        float puddleMask = 1.0 - smoothstep(u_GlobalWetness - 0.05, u_GlobalWetness + 0.05, noise);
        puddleMask *= isGround; 

        // 🌟 修正2：水坑底色必须更暗
        albedo = mix(albedo, albedo * 0.25, puddleMask);      
        
        // 🌟 修正3：深水坑必须是绝对光滑的镜面
        roughness = mix(roughness, 0.01, puddleMask);     
        metallic = mix(metallic, 0.0, puddleMask);           

        // 生成涟漪
        vec3 rippleNormal = ComputeRipples(pos, 4.0, u_Time);
        float rippleIntensity = 0.8; 

        // 🌟🌟🌟 核心法线修正：水坑抹平机制 (Normal Flattening) 🌟🌟🌟
        // 水会填平柏油路的坑洼，所以水坑的法线基础必须是绝对朝上的 vec3(0,1,0)！
        vec3 waterNormal = vec3(0.0, 1.0, 0.0);
        
        // 在绝对平坦的水面上加上波纹的扰动
        waterNormal.x += rippleNormal.x * rippleIntensity;
        waterNormal.z += rippleNormal.z * rippleIntensity;
        waterNormal = normalize(waterNormal);

        // 最终法线：干地用原本的粗糙法线，深水区用完美的涟漪水面法线！
        // 只有这样，你的 SSR 才能反射出赛博朋克的霓虹倒影！
        normal = normalize(mix(normal, waterNormal, puddleMask * 0.95)); 

        gORM = vec4(ao, roughness, metallic, puddleMask);
    }
    else 
    {
        gORM = vec4(ao, roughness, metallic, 0.0);
    }

    // 输出至 G-Buffer
    gPosition = fs_in.FragPos;
    gNormal = vec4(normal, emissiveMask);
    gAlbedo_parallaxShadow = vec4(albedo, parallaxShadow);
}