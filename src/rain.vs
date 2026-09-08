#version 430 core

struct Particle { vec4 Position; vec4 Velocity; };
layout (std430, binding = 0) buffer ParticleBuffer { Particle particles[]; };

layout (std140) uniform Matrices {
    mat4 projection; mat4 view; mat4 cleanProj; mat4 prevProj; mat4 prevView;   
};

uniform float time;
uniform vec3 cameraPos;

out vec2 ParticleUV;
out float IsSplash;
out vec3 WorldPos; 

void main()
{
    uint particleIndex = gl_VertexID / 6;
    uint vertexIndex = gl_VertexID % 6; 

    Particle p = particles[particleIndex];
    vec3 basePos = p.Position.xyz;
    float state = p.Position.w; 
    
    vec2 uvs[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    ParticleUV = uvs[vertexIndex];

    vec3 viewDir = normalize(cameraPos - basePos);
    vec3 upDir; vec3 rightDir;
    float width = 0.0; float height = 0.0;

    if (state < 0.5) // 下落雨丝
    {
        IsSplash = 0.0;
        // 🌟 关键修正：直接使用粒子真实速度（已包含动态风力）
        vec3 vel = p.Velocity.xyz;
        float speed = length(vel);
        if (speed < 0.01) speed = 25.0; // 防止静止粒子

        upDir = normalize(-vel);                  // 拉伸方向 = 运动反方向
        rightDir = normalize(cross(viewDir, upDir));

        width = 0.01;                             // 加宽，原来 0.005 太细
        height = speed * 0.035;                   // 长度与速度成正比，保证视觉连贯
    }
    else // 飞溅
    {
        IsSplash = 1.0;
        vec3 vel = p.Velocity.xyz;
        float speed = length(vel);
        upDir = normalize(vel);                  // 飞溅方向
        rightDir = normalize(cross(viewDir, upDir));

        width = 0.012;                           // 飞溅稍宽
        height = clamp(speed * 0.02, 0.03, 0.18);
    }

    // 构建四边形顶点
    vec2 quadPos = ParticleUV * 2.0 - 1.0;
    basePos += rightDir * quadPos.x * width + upDir * quadPos.y * height;

    WorldPos = basePos;
    gl_Position = cleanProj * view * vec4(basePos, 1.0);
}