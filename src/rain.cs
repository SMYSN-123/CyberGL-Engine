#version 430 core
layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle
{
    vec4 Position; // w: 0.0=下落, 1.0=飞溅
    vec4 Velocity; // w: 飞溅生命周期倒计时
};

layout (std430, binding = 0) buffer ParticleBuffer
{
    Particle particles[];
};

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
    mat4 cleanProj;
    mat4 prevProj;
    mat4 prevView;
};

uniform float deltaTime;
uniform float time;
uniform vec3 cameraPos;

uniform sampler2D gPositionMap;
uniform sampler2D gNormalMap;

uint wang_hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float randomFloat(uint seed)
{
    return float(seed) * (1.0 / 4294967296.0);
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    Particle p = particles[index];

    float state = p.Position.w; 
    float lifeTime = p.Velocity.w;

    uint seedState = index + floatBitsToUint(time);
    uint seedX = wang_hash(seedState * 1973u);
    uint seedY = wang_hash(seedState * 9277u);
    uint seedZ = wang_hash(seedState * 26699u);

    // ==========================================
    // 🌧️ 状态 0：物理下落与风力扰动
    // ==========================================
    if (state < 0.5) 
    {
        // 增加高频正弦风力波动，让雨丝下落带有轻微的飘逸感
        float windSpeed = time * 3.0;
        vec3 windForce = vec3(
            sin(windSpeed + p.Position.z * 0.1) * 1.0 + 3.0,   // 降低振幅和均值
            0.0,
            cos(windSpeed + p.Position.x * 0.1) * 0.8 - 1.0
        );
        
        vec3 currentVelocity = p.Velocity.xyz + windForce;
        p.Position.xyz += currentVelocity * deltaTime;

        bool collided = false;
        vec3 hitNormal = vec3(0.0, 1.0, 0.0);

        // 1. 基础世界高度边界
        if (p.Position.y < -5.0) {
            collided = true;
            p.Position.y = -4.95;
        }
        else 
        {
            // 2. G-Buffer 深度碰撞 (利用前向相机裁剪，防止雨穿透屋顶)
            vec4 clipSpace = projection * view * vec4(p.Position.xyz, 1.0);
            if (clipSpace.w > 0.0) 
            {
                vec3 ndc = clipSpace.xyz / clipSpace.w;
                vec2 uv = ndc.xy * 0.5 + 0.5; 

                if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) 
                {
                    vec3 sampledNormal = texture(gNormalMap, uv).xyz;
                    if (length(sampledNormal) > 0.5) 
                    {
                        vec3 scenePos = texture(gPositionMap, uv).xyz;
                        float sceneDist = length(scenePos - cameraPos);
                        float particleDist = length(p.Position.xyz - cameraPos);

                        // 动态厚度剔除：保证粒子只在撞击表面 1.2 米厚度内发生碰撞
                        float thickness = particleDist - sceneDist;
                        if (thickness > 0.0 && thickness < 1.2) {
                            collided = true;
                            hitNormal = normalize(sampledNormal); 
                            p.Position.xyz = scenePos + hitNormal * 0.02;
                        }
                    }
                }
            }
        }

        // 碰撞状态转移 -> 飞溅
        if (collided)
        {
            p.Position.w = 1.0; 
            p.Velocity.w = 0.12 + randomFloat(seedX) * 0.12; // 缩短水花寿命，使其更急促、更灵动
            
            // 沿着碰撞法线半球随机散射
            vec3 randDir = vec3(
                randomFloat(seedX) - 0.5,
                randomFloat(seedY) * 0.8 + 0.2, // 偏向法线上方
                randomFloat(seedZ) - 0.5
            );
            vec3 bounceDir = normalize(hitNormal + randDir * 0.6);
            float bounceForce = 3.0 + randomFloat(seedY) * 4.0; 
            p.Velocity.xyz = bounceDir * bounceForce;
        }
    }
    // ==========================================
    // 💦 状态 1：微型抛物线飞溅阶段
    // ==========================================
    else 
    {
        p.Velocity.y -= 19.8 * deltaTime; // 强化重力加速度，让水花呈更真实的弧度下坠
        p.Position.xyz += p.Velocity.xyz * deltaTime;
        p.Velocity.w -= deltaTime;

        // 寿命耗尽，重置回天空顶部，在相机视锥体周围重新随机分布
        if (p.Velocity.w <= 0.0) 
        {
            p.Position.w = 0.0; 
            p.Position.x = cameraPos.x + (randomFloat(seedX) - 0.5) * 120.0;
            p.Position.y = cameraPos.y + 25.0 + randomFloat(seedY) * 25.0;
            p.Position.z = cameraPos.z + (randomFloat(seedZ) - 0.5) * 120.0;
            p.Velocity.xyz = vec3(0.0, -28.0, 0.0); 
        }
    }

    particles[index] = p;
}