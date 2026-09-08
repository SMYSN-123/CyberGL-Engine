#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform float offset_x;
uniform float offset_y;

// ⚠️ 注意：这里的 screenTexture 已经不再是 HDR 原图了！
// 而是上面 post_processing.frag 输出的那张已经有 Bloom 和 色调映射的 LDR 贴图！
uniform sampler2D screenTexture; 

// --- FXAA 控制参数 ---
const float FXAA_REDUCE_MIN = 1.0/128.0;
const float FXAA_REDUCE_MUL = 1.0/8.0;
const float FXAA_SPAN_MAX = 8.0;

void main()
{
    // vec2 inverseScreenSize = vec2(offset_x, offset_y);

    // // 直接从处理好的 LDR 贴图里采样
    // vec3 rgbNW = texture(screenTexture, TexCoords + vec2(-1.0, -1.0) * inverseScreenSize).rgb;
    // vec3 rgbNE = texture(screenTexture, TexCoords + vec2( 1.0, -1.0) * inverseScreenSize).rgb;
    // vec3 rgbSW = texture(screenTexture, TexCoords + vec2(-1.0,  1.0) * inverseScreenSize).rgb;
    // vec3 rgbSE = texture(screenTexture, TexCoords + vec2( 1.0,  1.0) * inverseScreenSize).rgb;
    // vec3 rgbM  = texture(screenTexture, TexCoords).rgb;

    // // 计算亮度 (Luma)
    // vec3 luma = vec3(0.299, 0.587, 0.114);
    // float lumaNW = dot(rgbNW, luma);
    // float lumaNE = dot(rgbNE, luma);
    // float lumaSW = dot(rgbSW, luma);
    // float lumaSE = dot(rgbSE, luma);
    // float lumaM  = dot(rgbM,  luma);

    // float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    // float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // // 计算模糊方向
    // vec2 dir;
    // dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    // dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    // float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    // float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // dir = min(vec2(FXAA_SPAN_MAX,  FXAA_SPAN_MAX),
    //       max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * inverseScreenSize;

    // // 沿着边缘方向混合
    // vec3 rgbA = (1.0/2.0) * (
    //     texture(screenTexture, TexCoords + dir * (1.0/3.0 - 0.5)).rgb +
    //     texture(screenTexture, TexCoords + dir * (2.0/3.0 - 0.5)).rgb);

    // vec3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
    //     texture(screenTexture, TexCoords + dir * (0.0/3.0 - 0.5)).rgb +
    //     texture(screenTexture, TexCoords + dir * (3.0/3.0 - 0.5)).rgb);

    // float lumaB = dot(rgbB, luma);

    // if((lumaB < lumaMin) || (lumaB > lumaMax)) {
    //     FragColor = vec4(rgbA, 1.0);
    // } else {
    //     FragColor = vec4(rgbB, 1.0);
    // }

    // ==========================================
    // 🔪 FXAA 彻底下岗！
    // 因为我们的 TAA 已经在物理级别解决了锯齿问题，
    // 这里我们只需要原汁原味地把最终画面画到屏幕上！
    // ==========================================

    vec3 col = texture(screenTexture, TexCoords).rgb;
    FragColor = vec4(col, 1.0);
}