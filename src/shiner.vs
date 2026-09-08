#version 330 core
layout (location = 0) in vec3 aPos;

layout (std140) uniform Matrices
{
    mat4 projection; // 槽位0：带抖动的矩阵！(给全场所有东西画图用)
    mat4 view;       // 槽位1：当前视图矩阵
    mat4 cleanProj;  // 槽位2：干净无抖动投影！(专门给 G-Buffer 算当前物理坐标用)
    mat4 prevProj;   // 槽位3：上一帧干净投影！(专门给 G-Buffer 算历史物理坐标用)
    mat4 prevView;   // 槽位4：上一帧视图矩阵
};

uniform mat4 model;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}