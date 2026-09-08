#version 430 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D currentFrameTex; // 当前帧刚刚画好的、带狗牙、带相机抖动的画面
uniform sampler2D historyFrameTex; // 上一帧抗好锯齿、且被拉回【绝对静止】的历史画面
uniform sampler2D velocityTex;     // 物理速度图

// 🌟 只接收当前帧的抖动偏移，不需要历史抖动！
uniform vec2 u_JitterUV;

// ==========================================
// 🛠️ YCoCg 色彩空间转换矩阵 (大厂标准)
// 作用：解耦亮度(Y)与色度，防止邻域裁剪时产生颜色溢出和色偏
// ==========================================
vec3 RGBToYCoCg(vec3 rgb) {
    float Y  = dot(rgb, vec3( 0.25, 0.5,  0.25));
    float Co = dot(rgb, vec3( 0.5,  0.0, -0.5));
    float Cg = dot(rgb, vec3(-0.25, 0.5, -0.25));
    return vec3(Y, Co, Cg);
}

vec3 YCoCgToRGB(vec3 YCoCg) {
    float Y  = YCoCg.x;
    float Co = YCoCg.y;
    float Cg = YCoCg.z;
    float R = Y - Cg + Co;
    float G = Y + Cg;
    float B = Y - Cg - Co;
    return vec3(R, G, B);
}

void main()
{
    // =======================================================
    // 🌟 核心破局 1：剥离相机的物理抖动 (Static Space Un-jitter)
    // 架构精髓：透视矩阵除法会导致正向偏移变成负的 UV 偏移。
    // 因此使用减号，反向寻找它原本干净、静止的物理颜色！
    // 这行代码将 TAA 彻底拉入了“绝对静止空间”！
    // =======================================================
    vec2 jitteredUV = TexCoords - u_JitterUV;

    // 当前帧和速度都是抖动的，必须用剥离抖动后的 jitteredUV 去采样，提取出绝对静止的数据
    vec2 velocity = texture(velocityTex, jitteredUV).rg;
    vec3 currentColor = texture(currentFrameTex, jitteredUV).rgb;

    // =======================================================
    // 🌟 核心破局 2：大道至简的历史坐标
    // 既然当前颜色已经被拉回静止，写入的历史缓存也永远是静止的。
    // 直接用静止屏幕坐标减去速度，公式无比纯净，摒弃复杂的抖动补偿！
    // =======================================================
    vec2 prevUV = TexCoords - velocity;

    // 边界保护：如果历史找回来的坐标超出了屏幕，拒绝混合，防止画面边缘产生黑边拉丝
    if(prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        FragColor = vec4(currentColor, 1.0);
        return;
    }
    vec3 historyColor = texture(historyFrameTex, prevUV).rgb;

    // =======================================================
    // 🛡️ 方差裁剪 (Variance Clipping) 包容细微闪烁
    // 为什么不用传统的 Min/Max？因为方差能计算出局部的柔和统计学边界，有效防止微小的高频闪烁。
    // =======================================================
    vec2 texelSize = 1.0 / textureSize(currentFrameTex, 0);
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);

    // 🌟 必须围绕 jitteredUV 采样，让整个包围盒的计算都在【绝对静止空间】中进行
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            vec3 neighborColor = texture(currentFrameTex, jitteredUV + offset).rgb;
            vec3 neighborYCoCg = RGBToYCoCg(neighborColor);
            m1 += neighborYCoCg;
            m2 += neighborYCoCg * neighborYCoCg;
        }
    }

    vec3 mu = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mu * mu, 0.0));

    // 1.25 倍标准差：在抗锯齿平滑度与消除残影之间取得最完美的平衡
    float gamma = 1.25; 
    vec3 colorMin = mu - gamma * sigma;
    vec3 colorMax = mu + gamma * sigma;

    // 将历史颜色转入 YCoCg 空间，并接受“结界”的审判！
    vec3 historyYCoCg = RGBToYCoCg(historyColor);
    
    // 🔪 灵魂裁决：把不匹配的历史残影强行按回当前环境允许的色彩范围内，瞬间消灭 Ghosting！
    historyYCoCg = clamp(historyYCoCg, colorMin, colorMax);
    
    // 审判结束，转回 RGB 空间
    historyColor = YCoCgToRGB(historyYCoCg);

    // ==========================================
    // ⏳ 动态响应的指数移动平均混合 (Dynamic EMA)
    // ==========================================
    // 基础信任度：95% 的能量来自于极其平滑的历史积累，5% 接收当前狗牙画面
    float blendAlpha = 0.05; 

    // 如果画面发生了剧烈移动 (物理速度过大)，稍微增加当前帧的权重，让画面响应更快，防止大动态下的画面模糊
    float velocityLength = length(velocity);
    if(velocityLength > 0.01) {
        blendAlpha = mix(0.05, 0.2, smoothstep(0.01, 0.01, velocityLength));
    }

    // 混合！此时 resolvedColor 是没有狗牙、没有残影、绝对静止的完美像素！
    vec3 resolvedColor = mix(historyColor, currentColor, blendAlpha);

    FragColor = vec4(resolvedColor, 1.0);
}