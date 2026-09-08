#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <random>
#include "Shader.h"
#include "Camera.h"
#include "Mesh.h"
#include "Model.h"
#include "loadTexture.h"
#include "FPSCounter.h"
#include "Skybox.h"
#include "SkyboxHelper.h"
#include "PhysicallyBasedBloom.h"
#include "PBRMaterial.h"
#include "RainyAlleyScene.h"
#include "ComputeShader.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// 1. 这里写窗口大小改变的回调函数 (framebuffer_size_callback)
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void renderCube();
void renderScreenQuad();
std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& projview);
std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
std::vector<glm::mat4> getLightSpaceMatrices();
void renderSphere();
float lerp(float a, float b, float f);
float GetHaltonValue(int index, int base);

RainyAlleyScene rainyAlley;

unsigned int SCR_WIDTH = 800;
unsigned int SCR_HEIGHT = 600;

// G-buffer

// Intermediate FBO: 用于接收 MSAA 还原后的普通图像 (做后处理)
unsigned int hdrFBO;
unsigned int hdrRBO;

unsigned int screenTexture[1];                  // 普通纹理数组 (0:场景)

unsigned int taaTextures[2];

unsigned int postProcessTexture;

unsigned int gPosition, gNormal, gAlbedo_parallaxShadow, gORM, gVelocity;
unsigned int gRbo;

unsigned int ssaoColorBuffer;
unsigned int ssaoColorBufferBlur;

unsigned int ssrTraceFBO;
unsigned int ssrTraceTexture;

unsigned int hdrColorCopyTexture;

unsigned int floorVAO;

unsigned int normalMapVAO;
unsigned int normalMapVBO;

Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

float deltaTime = 0.0f;
float lastFrame = 0.0f;

bool firstMouse = true;
float lastY = SCR_HEIGHT / 2.0f;
float lastX = SCR_WIDTH / 2.0f;

bool isMouseCaptured  = true;
bool isFullscreen = false;

int windowedPosX;
int windowedPosY;
int windowedWidth;
int windowedHeight;

bool blinn = false;
bool blinnKeyPressed = false;

bool showDebugDepth = false;
bool showDebugDepthKeyPressed = false;

bool bloom = true;
bool bloomKeyPressed = false;

bool u_DebugEmissiveUV = false;
bool u_DebugEmissiveUVKeyPressed = false;

bool pKeyPressed = false;

const unsigned int SHADOW_WIDTH = 4096;
const unsigned int SHADOW_HEIGHT = 4096;

const float cameraNearPlane = 0.1f;
const float cameraFarPlane = 1000.0f;

std::vector<float> shadowCascadeLevels{cameraFarPlane / 25.0f, cameraFarPlane / 10.0f, cameraFarPlane / 2.0f, cameraFarPlane / 1.0f};

const glm::vec3 lightPos(1.57174f, 23.164f, 1.64255f);

const glm::vec3 lightDir = normalize(lightPos - camera.Position);

BloomRenderer bloomRenderer;

int main() {

    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "MyEngine", NULL, NULL);

    if(window == NULL)
    {
        std::cout<<"Failed to create GLFW window"<<"\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // 0 表示关闭垂直同步，让帧率彻底放飞自我！

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout<<"Failed to initialize GLAD"<<"\n";
        return -1;
    }

    // 🌟 初始化 ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // 开启键盘控制 (可选)
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 设置 ImGui 的主题风格 (经典的极客暗黑风)
    ImGui::StyleColorsDark();

    // 初始化平台/渲染器后端
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430"); // 告诉 ImGui 你用的 OpenGL 版本

    // [在这里定义 UI 变量，避免每次循环重新初始化]
    static float ui_GlobalWetness = 0.759f;
    static float ui_Exposure = 0.791f;
    static bool  ui_EnableSSR = true;
    static bool  ui_EnableVolumetric = true;
    static bool  ui_EnableBloom = true;
    static bool enableTAA_Jitter = true; // 你可以用 ImGui 加个开关控制它

    FPSCounter fpsCounter;

    Shader screenShader("../src/screen.vs", "../src/screen.fs");
    Shader shiner("../src/shiner.vs", "../src/shiner.fs");
    Shader csmShadowDepthShader("../src/csm_shadows_depth.vs", "../src/csm_shadows_depth.fs", "../src/csm_shadows_depth.gs");
    Shader equirectangularToCubemapShader("../src/equirectangular_to_cubemap.vs", "../src/equirectangular_to_cubemap.fs");
    Shader backgroundShader("../src/background.vs", "../src/background.fs");
    Shader irradianceConvolutionShader("../src/irradiance_convolution.vs", "../src/irradiance_convolution.fs");
    Shader prefilterShader("../src/prefilter.vs", "../src/prefilter.fs");
    Shader brdfShader("../src/brdf.vs", "../src/brdf.fs");
    Shader debugShader("../src/screen.vs", "../src/debug.fs");
    Shader glassballShader("../src/glass_ball.vs", "../src/glass_ball.fs");
    Shader gBufferShader("../src/g_buffer.vs", "../src/g_buffer.fs");
    Shader deferredPurePBRShader("../src/deferred_pure_pbr.vs", "../src/deferred_pure_pbr.fs");
    Shader postProcessShader("../src/screen.vs", "../src/post_processing.fs");
    Shader ssaoShader("../src/screen.vs", "../src/ssao.fs");
    Shader ssaoBlurShader("../src/screen.vs", "../src/ssao_blur.fs");
    Shader ssrRayMarchingTraceShader("../src/screen.vs", "../src/ssr_ray_marching.fs");
    Shader ssrCompositeShader("../src/screen.vs", "../src/ssr_composite.fs");
    Shader volumetricLightShader("../src/deferred_pure_pbr.vs", "../src/volumetric_light.fs");
    Shader rainShader("../src/rain.vs", "../src/rain.fs");
    Shader taaResolveShader("../src/deferred_pure_pbr.vs", "../src/taa_resolve.fs");
    Shader transparentForwardShader("../src/transparentForwardShader.vs", "../src/transparentForwardShader.fs");

    ComputeShader computeShader("../src/compute.cs");
    ComputeShader rainComputeShader("../src/rain.cs");

    // Lens Dirt
    loadTexture Lens_Dirt("../extern/Lens Dirt/lens_dirt.jpg", false);

    rainyAlley.Init();

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // 修复立方体贴图接缝

    glEnable(GL_MULTISAMPLE);

    glDisable(GL_CULL_FACE);

    glEnable(GL_PROGRAM_POINT_SIZE);

    unsigned int uniformBlockIndexshinerShader = glGetUniformBlockIndex(shiner.ProgramID, "Matrices");
    unsigned int uniformBlockIndexdeferredPurePBRShader_matrices = glGetUniformBlockIndex(deferredPurePBRShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexbackgroundShader = glGetUniformBlockIndex(backgroundShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexglassballShader = glGetUniformBlockIndex(glassballShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexssaoShader = glGetUniformBlockIndex(ssaoShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexssrRayMarchingTraceShader = glGetUniformBlockIndex(ssrRayMarchingTraceShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexssrCompositeShader = glGetUniformBlockIndex(ssrCompositeShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexvolumetricLightShader = glGetUniformBlockIndex(volumetricLightShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexrainShader = glGetUniformBlockIndex(rainShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexrainComputeShader = glGetUniformBlockIndex(rainComputeShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndextransparentForwardShader = glGetUniformBlockIndex(transparentForwardShader.ProgramID, "Matrices");

    unsigned int uniformBlockIndexcsmShadowDepthShader = glGetUniformBlockIndex(csmShadowDepthShader.ProgramID, "LightSpaceMatrices");
    unsigned int uniformBlockIndexdeferredPurePBRShader_light = glGetUniformBlockIndex(deferredPurePBRShader.ProgramID, "LightSpaceMatrices");
    unsigned int uniformBlockIndexvolumetricLightShader_light = glGetUniformBlockIndex(volumetricLightShader.ProgramID, "LightSpaceMatrices");

    glUniformBlockBinding(shiner.ProgramID, uniformBlockIndexshinerShader, 0);
    glUniformBlockBinding(deferredPurePBRShader.ProgramID, uniformBlockIndexdeferredPurePBRShader_matrices, 0);
    glUniformBlockBinding(backgroundShader.ProgramID, uniformBlockIndexbackgroundShader, 0);
    glUniformBlockBinding(glassballShader.ProgramID, uniformBlockIndexglassballShader, 0);
    glUniformBlockBinding(ssaoShader.ProgramID, uniformBlockIndexssaoShader, 0);
    glUniformBlockBinding(ssrCompositeShader.ProgramID, uniformBlockIndexssrCompositeShader, 0);
    glUniformBlockBinding(volumetricLightShader.ProgramID, uniformBlockIndexvolumetricLightShader, 0);
    glUniformBlockBinding(ssrRayMarchingTraceShader.ProgramID, uniformBlockIndexssrRayMarchingTraceShader, 0);
    glUniformBlockBinding(rainShader.ProgramID, uniformBlockIndexrainShader, 0);
    glUniformBlockBinding(rainComputeShader.ProgramID, uniformBlockIndexrainComputeShader, 0);
    glUniformBlockBinding(transparentForwardShader.ProgramID, uniformBlockIndextransparentForwardShader, 0);

    glUniformBlockBinding(csmShadowDepthShader.ProgramID, uniformBlockIndexcsmShadowDepthShader, 1);
    glUniformBlockBinding(deferredPurePBRShader.ProgramID, uniformBlockIndexdeferredPurePBRShader_light, 1);
    glUniformBlockBinding(volumetricLightShader.ProgramID, uniformBlockIndexvolumetricLightShader_light, 1);

    // G-buffer
    unsigned int gBuffer;
    glGenFramebuffers(1, &gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

    // 1. 位置颜色缓冲 (使用 16F 保证精度)
    glGenTextures(1, &gPosition);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition, 0);

    // 2. 法线颜色缓冲 (使用 16F 保证法线精度，防止光照出现条带)
    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

    // 3. 颜色和视差阴影缓冲 (RGBA，普通精度即可)
    glGenTextures(1, &gAlbedo_parallaxShadow);
    glBindTexture(GL_TEXTURE_2D, gAlbedo_parallaxShadow);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedo_parallaxShadow, 0);

    // 4. PBR 参数缓冲 ORM (RGB，普通精度即可)
    glGenTextures(1, &gORM);
    glBindTexture(GL_TEXTURE_2D, gORM);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gORM, 0);

    // 5. 创建速度贴图 gVelocity (2通道，16位浮点)
    glGenTextures(1, &gVelocity);
    glBindTexture(GL_TEXTURE_2D, gVelocity);
    // 🌟 重点 1：使用 GL_RG16F，保证高精度且只占用 2 个通道的带宽
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RG, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 🌟 重点 2：将其挂载到 GL_COLOR_ATTACHMENT4 (第5个附件)
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, gVelocity, 0);

    // 告诉 OpenGL 我们要渲染到这 5 个附件
    unsigned int attachments[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
    glDrawBuffers(5, attachments);

    // gRbo
    glGenRenderbuffers(1, &gRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, gRbo);

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "Framebuffer not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // hdr Framebuffer
    glGenFramebuffers(1, &hdrFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);

    glGenTextures(1, screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture[0]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture[0], 0);

    // 🔥 新增：给 hdrFBO 挂载一个深度 RBO，专门用来接收 G-Buffer Blit 过来的深度
    glGenRenderbuffers(1, &hdrRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, hdrRBO);
    // 使用 GL_DEPTH_COMPONENT24 精度一般就够了，如果需要更精确可以用 GL_DEPTH_COMPONENT32F
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, SCR_WIDTH, SCR_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdrRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Intermediate Framebuffer is not complete!" << std::endl;

    // ubo
    unsigned int uboMatrices;
    glGenBuffers(1, &uboMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
    glBufferData(GL_UNIFORM_BUFFER, 5 * sizeof(glm::mat4), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBufferRange(GL_UNIFORM_BUFFER, 0, uboMatrices, 0, 5 * sizeof(glm::mat4));

    //light ubo
    unsigned int lightUboMatrices;
    glGenBuffers(1, &lightUboMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, lightUboMatrices);
    glBufferData(GL_UNIFORM_BUFFER, 16 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBufferRange(GL_UNIFORM_BUFFER, 1, lightUboMatrices, 0, 16 * sizeof(glm::mat4));

    // light FBO
    unsigned int lightFBO;
    glGenFramebuffers(1, &lightFBO);

    unsigned int lightDepthMaps;
    glGenTextures(1, &lightDepthMaps);

    glBindTexture(GL_TEXTURE_2D_ARRAY, lightDepthMaps);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, SHADOW_WIDTH, SHADOW_HEIGHT, static_cast<int>(shadowCascadeLevels.size()) + 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    constexpr float bordercolor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bordercolor);

    glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, lightDepthMaps, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
        throw 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // equirectangularMap
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrComponents;
    // float *data = stbi_loadf("../src/overcast_soil_puresky_4k.hdr", &width, &height, &nrComponents, 0);
    float *data = stbi_loadf("../src/the_sky_is_on_fire_4k.hdr", &width, &height, &nrComponents, 0);
    // float *data = stbi_loadf("../src/viale_giuseppe_garibaldi_4k.hdr", &width, &height, &nrComponents, 0);

    unsigned int equirectangularMap;
    if (data)
    {
        glGenTextures(1, &equirectangularMap);
        glBindTexture(GL_TEXTURE_2D, equirectangularMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data); 

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Failed to load HDR image." << std::endl;
    }

    // capture FBO
    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    unsigned int envCubemap;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for(unsigned int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = 
    {
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    equirectangularToCubemapShader.use();

    equirectangularToCubemapShader.setInt("equirectangularMap", 0);
    equirectangularToCubemapShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, equirectangularMap);

    glViewport(0, 0, 512, 512);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for(unsigned int i = 0; i < 6; i++)
    {
        equirectangularToCubemapShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    unsigned int irradianceMap;
    glGenTextures(1, &irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);

    irradianceConvolutionShader.use();
    
    irradianceConvolutionShader.setInt("environmentMap", 0);
    irradianceConvolutionShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glViewport(0, 0, 32, 32);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        irradianceConvolutionShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int prefilterMap;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    prefilterShader.use();

    prefilterShader.setInt("environmentMap", 0);
    prefilterShader.setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMiplevels = 5;
    for(unsigned int mip = 0; mip < maxMiplevels; mip++)
    {
        unsigned int mipWidth = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = static_cast<float>(mip) / static_cast<float>(maxMiplevels - 1);
        prefilterShader.setFloat("roughness", roughness);
        for(unsigned int i = 0; i < 6; i++)
        {
            prefilterShader.setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap, mip);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int brdfLUTTexture;
    glGenTextures(1, &brdfLUTTexture);

    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

    brdfShader.use();

    glViewport(0, 0, 512, 512);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderScreenQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    bloomRenderer.Init(SCR_WIDTH, SCR_HEIGHT);

    // --- 🌟 新增：后期处理中间缓冲 (Post-Process FBO) ---
    unsigned int postProcessFBO;
    glGenFramebuffers(1, &postProcessFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, postProcessFBO);

    glGenTextures(1, &postProcessTexture);
    glBindTexture(GL_TEXTURE_2D, postProcessTexture);
    // 注意：这里用 GL_RGB 就够了 (普通 8 位精度)，因为经过 ToneMapping 后它已经是 LDR 颜色了
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postProcessTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: PostProcess Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f); // 随机浮点数，范围0.0 - 1.0
    std::default_random_engine generator;
    std::vector<glm::vec3> ssaoKernel;
    for(unsigned int i = 0; i < 64; i++)
    {
        glm::vec3 sample(
            randomFloats(generator) * 2.0f - 1.0f, // x: -1.0 ~ 1.0
            randomFloats(generator) * 2.0f - 1.0f, // y: -1.0 ~ 1.0
            randomFloats(generator)                // z:  0.0 ~ 1.0
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(generator); // 使样本点分布在半球内，靠近原点的点更密集
        float scale = static_cast<float>(i) / 64.0f;
        scale = lerp(0.1f, 1.0f, scale * scale); // 通过插值函数使得靠近原点的点更密集
        sample *= scale;
        ssaoKernel.push_back(sample);
    }

    std::vector<glm::vec3> ssaoNoise;
    for(unsigned int i = 0; i < 16; i++)
    {
        glm::vec3 noise(
            randomFloats(generator) * 2.0f - 1.0f, // x: -1.0 ~ 1.0
            randomFloats(generator) * 2.0f - 1.0f, // y: -1.0 ~ 1.0
            0.0f                                   // z:  0.0 (旋转只在切线空间的 xy 平面内进行)
        );
        ssaoNoise.push_back(noise);
    }

    unsigned int noiseTexture;
    glGenTextures(1, &noiseTexture);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, ssaoNoise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    unsigned int ssaoFBO;
    glGenFramebuffers(1, &ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);

    glGenTextures(1, &ssaoColorBuffer);
    glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBuffer, 0);

    unsigned int ssaoBlurFBO;
    glGenFramebuffers(1, &ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);

    glGenTextures(1, &ssaoColorBufferBlur);
    glBindTexture(GL_TEXTURE_2D, ssaoColorBufferBlur);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBufferBlur, 0);

    glGenFramebuffers(1, &ssrTraceFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssrTraceFBO);

    glGenTextures(1, &ssrTraceTexture);
    glBindTexture(GL_TEXTURE_2D, ssrTraceTexture);
    // internalFormat 是 GL_RGBA16F，format 必须是 GL_RGBA！
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 边界设置为 Clamp，防止光线步进时飞出屏幕外产生奇怪的连线
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssrTraceTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: SSR Trace Framebuffer is not complete!" << std::endl;

    // 🌟 新增：用于 SSR Mipmap 模糊的 HDR 备份纹理
    glGenTextures(1, &hdrColorCopyTexture);
    glBindTexture(GL_TEXTURE_2D, hdrColorCopyTexture);
    // 格式必须与你的 screenTexture[0] 保持一致
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    
    // ⚠️ 极其关键：因为我们后续要调用 glGenerateMipmap 并使用 textureLod 采样，
    // MIN_FILTER 必须设置为带有 MIPMAP 的选项！
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    struct Particle
    {
        glm::vec4 Position;
        glm::vec4 Velocity;
    };

    const int NUM_PARTICLES = 150000;
    std::vector<Particle> particles(NUM_PARTICLES);
    for (int i = 0; i < NUM_PARTICLES; ++i) {
        float x = (rand() % 10000 / 100.0f) - 50.0f;
        float y = (rand() % 4000 / 100.0f) + 10.0f; // 随机高度 10~50
        float z = (rand() % 10000 / 100.0f) - 50.0f;
        // 🌟 修正：最后一个参数改成 0.0f，代表出厂状态是“下落”
        particles[i].Position = glm::vec4(x, y, z, 0.0f);
        // 给一个极快的下落速度
        particles[i].Velocity = glm::vec4(0.0f, -25.0f, 0.0f, 0.0f);
    }

    unsigned int ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, ssbo, 0, particles.size() * sizeof(Particle));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    unsigned int emptyVAO;
    glGenVertexArrays(1, &emptyVAO);

    // ============================================================================
    // 🌟 赛博朋克光影调色盘：11 盏路灯 + 10 盏霓虹灯
    // ============================================================================

    // 1. 保留原有的 11 盏路灯 (提供基础暖色照明)
    std::vector<glm::vec3> streetLightPositions = {
        glm::vec3(4.00213f, 6.01378f, -4.17447f),
        glm::vec3(-2.30235f, 6.04963f, 2.58851f),
        glm::vec3(5.18049f, 5.99219f, 2.49504f),
        glm::vec3(-15.6622f, 5.80179f, -2.11526f),
        glm::vec3(-22.5088f, 5.77675f, -0.634306f),
        glm::vec3(-18.1922f, 5.79403f, 3.74866f),
        glm::vec3(-22.6498f, 6.0153f, 7.21791f),
        glm::vec3(-20.6705f, 5.79533f, 13.2432f),
        glm::vec3(-22.9742f, 6.02914f, -9.53011f),
        glm::vec3(-18.0152f, 5.81687f, -15.6972f),
        glm::vec3(20.7122f, 6.01481f, 14.2775f)
    };

    struct Light
    {
        glm::vec4 Position; // XYZ: 位置, W: 衰减半径 (Radius)
        glm::vec4 Color;    // RGB: 颜色, W: 发光强度 (Intensity)
    };

    std::vector<Light> lightData;

    // --- A. 装配路灯 ---
    for (size_t i = 0; i < streetLightPositions.size(); i++)
    {
        Light light;
        float lightRadius = 15.0f; // 路灯照亮范围较大
        float lightIntensity = 10.0f; // 亮度中等，作为环境底光
        light.Position = glm::vec4(streetLightPositions[i], lightRadius);
        light.Color = glm::vec4(glm::vec3(255.0f, 243.0f, 229.0f) / 255.0f, lightIntensity);
        lightData.push_back(light);
    }

    std::vector<glm::vec3> neonPositions = {
        glm::vec3(5.49142f, 4.71312f, -4.72402f),
        glm::vec3(4.57291f, 4.6813f, -6.17225f),
        glm::vec3(-7.14997f, 5.27681f, 3.50051f),
        glm::vec3(-2.53438f, 8.28421f, 8.08619f),
        glm::vec3(8.85266f, 4.7669f, 2.07775f),
        glm::vec3(-14.1141f, 4.59362f, 2.05234f),
        glm::vec3(20.7897f, -1.2781f, 4.3236f),
        glm::vec3(-23.5339f, 6.03975f, 3.09581f),
        glm::vec3(-11.0537f, 6.29027f, -3.5572f),
        glm::vec3(-6.50755f, 11.0187f, 5.63222f),
        glm::vec3(19.70567f, -2.16069f, 8.27421f)
    };

    // 🌟 新增：手动一对一指定颜色，告别取余算法！(假设颜色范围是 0~255)
    // 如果你发现图二的前两个灯颜色反了，只需要把前两个 vec3 调换一下位置即可！
    std::vector<glm::vec3> neonColors = {
        glm::vec3(187.0f, 255.0f, 255.0f), // 1. 高饱和度电磁青
        glm::vec3(255.0f, 20.0f, 120.0f),  // 2. 热烈霓虹粉红
        glm::vec3(255.0f, 140.0f, 0.0f),   // 3. huang
        glm::vec3(2.0f, 250.0f, 124.0f),   // 4. lv
        glm::vec3(57.0f, 255.0f, 20.0f),   // 5. 毒气荧光绿
        glm::vec3(255.0f, 140.0f, 0.0f),   // 6. 热烈暖黄色
        glm::vec3(255.0f, 201.0f, 64.0f),   // 7. 
        glm::vec3(255.0f, 255.0f, 255.0f), // 8. bai
        glm::vec3(150.0f, 200.0f, 255.0f), // 9. 冷白/清蓝色
        glm::vec3(255.0f, 0.0f, 100.0f),   // 10. 高处广告牌 (高强度赛博洋红)
        glm::vec3(0.0f, 143.0f, 199.0f)
    };

    // ... 在组装 lightData 的 B 阶段：
    for (size_t i = 0; i < neonPositions.size(); i++)
    {
        Light light;
        float radius = 10.0f;
        float intensity = 20.0f;

        if (i == 9) {
            radius = 10.0f;
            intensity = 15.0f;
        }

        light.Position = glm::vec4(neonPositions[i], radius);
        // 🌟 直接读取上面指定的专属颜色！
        light.Color = glm::vec4(neonColors[i] / 255.0f, intensity);
        lightData.push_back(light);
    }

    // --- C. 构建并绑定 SSBO ---
    unsigned int lightSSBO;
    glGenBuffers(1, &lightSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, lightData.size() * sizeof(Light), lightData.data(), GL_DYNAMIC_DRAW);
    // 🌟 重点：绑定到 1 号位，绝不能和粒子的 0 号位冲突！
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, lightSSBO, 0, lightData.size() * sizeof(Light));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // ============================================================================
    // 🌟 TAA 历史缓冲 Ping-Pong Framebuffers 初始化
    // ============================================================================
    unsigned int taaFBOs[2];

    glGenFramebuffers(2, taaFBOs);
    glGenTextures(2, taaTextures);

    for (unsigned int i = 0; i < 2; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, taaFBOs[i]);
        glBindTexture(GL_TEXTURE_2D, taaTextures[i]);
        
        // 使用 GL_RGBA16F HDR 浮点精度
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, taaTextures[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "ERROR::TAA_FRAMEBUFFER:: Framebuffer " << i << " is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ============================================================================
    // 🌟 2. 准备读写指针 (Ping-Pong 句柄)
    // ============================================================================
    unsigned int taaReadFBO  = taaFBOs[0];
    unsigned int taaReadTex  = taaTextures[0];

    unsigned int taaWriteFBO = taaFBOs[1];
    unsigned int taaWriteTex = taaTextures[1];

    // 🌟 用一个布尔值记录：我们是否需要执行“冷启动保护”？
    bool isTAAInitialized = false;

    // 解绑 VBO (可选，是个好习惯)
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    debugShader.use();
    debugShader.setInt("debugTexture", 0);

    backgroundShader.use();
    backgroundShader.setInt("environmentMap", 0);

    csmShadowDepthShader.use();

    gBufferShader.use();
    // gBufferShader.setInt("albedoMap", 0);
    // gBufferShader.setInt("normalMap", 1);
    // gBufferShader.setInt("depthMap", 2);
    // gBufferShader.setInt("metallicMap", 3);
    // gBufferShader.setInt("roughnessMap", 4);
    // gBufferShader.setInt("aoMap", 5);
    // gBufferShader.setInt("emissiveMap", 6);
    gBufferShader.setInt("puddleNoiseMap", 15);

    ssaoShader.use();
    for (unsigned int i = 0; i < 64; ++i)
    {
    ssaoShader.setVec3("samples[" + std::to_string(i) + "]", ssaoKernel[i]);
    }
    ssaoShader.setInt("gPosition", 0);
    ssaoShader.setInt("gNormal", 1);
    ssaoShader.setInt("texNoise", 2);

    ssaoBlurShader.use();
    ssaoBlurShader.setInt("ssaoInput", 0);

    deferredPurePBRShader.use();
    deferredPurePBRShader.setInt("gPosition", 0);
    deferredPurePBRShader.setInt("gNormal", 1);
    deferredPurePBRShader.setInt("gAlbedo_parallaxShadow", 2);
    deferredPurePBRShader.setInt("gORM", 3);
    deferredPurePBRShader.setInt("irradianceMap", 6);
    deferredPurePBRShader.setInt("prefilterMap", 7);
    deferredPurePBRShader.setInt("brdfLUT", 8);
    deferredPurePBRShader.setInt("shadowMap", 10);
    deferredPurePBRShader.setInt("ssaoTexture", 11);
    // 核心修复：延迟管线同样需要采样噪音，必须与 G-Buffer 保持一致的高位槽位！
    deferredPurePBRShader.setInt("puddleNoiseMap", 15);

    transparentForwardShader.use();
    // transparentForwardShader.setInt("albedoMap", 0);
    // transparentForwardShader.setInt("normalMap", 1);
    // transparentForwardShader.setInt("metallicMap", 2);
    // transparentForwardShader.setInt("emissiveMap", 3);

    ssrRayMarchingTraceShader.use();
    ssrRayMarchingTraceShader.setInt("gPosition", 0);
    ssrRayMarchingTraceShader.setInt("gNormal", 1);
    ssrRayMarchingTraceShader.setInt("gORM", 2);

    ssrCompositeShader.use();
    ssrCompositeShader.setInt("ssrTraceTexture", 0);
    ssrCompositeShader.setInt("hdrColorCopyTexture", 1);
    ssrCompositeShader.setInt("gPosition", 2);
    ssrCompositeShader.setInt("gNormal", 3);
    ssrCompositeShader.setInt("gORM", 4);
    ssrCompositeShader.setInt("gAlbedo_parallaxShadow", 5);

    postProcessShader.use();
    postProcessShader.setInt("screenTexture", 0);
    postProcessShader.setInt("bloomBlur", 1);
    postProcessShader.setInt("dirtMaskTexture", 2);

    screenShader.use();
    screenShader.setInt("screenTexture", 0);

    // 在 while 循环外面定义一个帧计数器
    static int frameCounter = 0;

    // 在 while 循环外面定义：
    glm::mat4 prevView = glm::mat4(1.0f);
    glm::mat4 prevProj = glm::mat4(1.0f);
    bool isFirstFrame = true;

    static float prevJitterU = 0.0f;
    static float prevJitterV = 0.0f;

    // 解绑 VAO
    glBindVertexArray(0);

    // --- 渲染循环 ---
    while (!glfwWindowShouldClose(window)) 
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        // =======================================================
        // 🌟 [ImGui 新增] 第二步：每帧开头，开启新的 ImGui 帧
        // =======================================================
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 构建赛博控制台面板
        ImGui::Begin("Cyberpunk Engine Control Panel");
        
        ImGui::Text("Performance: %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Separator();

        ImGui::Text("Environment");
        ImGui::SliderFloat("Global Wetness", &ui_GlobalWetness, 0.0f, 1.0f);
        
        ImGui::Text("Post-Processing & Effects");
        ImGui::SliderFloat("Exposure", &ui_Exposure, 0.1f, 3.0f);
        ImGui::Checkbox("Enable Bloom", &ui_EnableBloom);
        ImGui::Checkbox("Enable SSR", &ui_EnableSSR);
        ImGui::Checkbox("Enable Volumetric Fog", &ui_EnableVolumetric);
        ImGui::Checkbox("Enable TAA Jitter", &enableTAA_Jitter);

        ImGui::End();

        // 1. 我们通常用 16 个点作为一个完整的采样周期 (16x TAA)
        // 为什么要 +1？因为 Halton 序列的 index 传 0 会返回 0，必须从 1 开始！
        int haltonIndex = (frameCounter % 16) + 1; 

        // 2. 拿到在 [0.0, 1.0] 范围内的两个点
        float haltonX = GetHaltonValue(haltonIndex, 2);
        float haltonY = GetHaltonValue(haltonIndex, 3);
        
        // 帧数递增，留给下一帧用
        frameCounter++;

        // 更新 UBO (矩阵)
        const auto lightMatrices = getLightSpaceMatrices();
        glBindBuffer(GL_UNIFORM_BUFFER, lightUboMatrices);
        for(size_t i = 0; i < lightMatrices.size(); i++)
        {
            glBufferSubData(GL_UNIFORM_BUFFER, i * sizeof(glm::mat4), sizeof(glm::mat4), lightMatrices.data());
        }
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // 1. 算出【干净的】投影和视图矩阵 (绝对不要加 Halton 抖动！)
        glm::mat4 cleanProj = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH/(float)SCR_HEIGHT, 0.1f, 1000.0f);
        glm::mat4 cleanView = camera.GetViewMatrix();

        // 如果是第一帧，上一帧就等于当前帧（防止画面初始时乱飞）
        if (isFirstFrame)
        {
            prevProj = cleanProj;
            prevView = cleanView;
            isFirstFrame = false;
        }

        glm::mat4 jitterProj = cleanProj;
        float jitterU = 0.0f; 
        float jitterV = 0.0f;

        if (enableTAA_Jitter)
        {
            float jitterX = (haltonX * 2.0f - 1.0f) / (float)SCR_WIDTH;
            float jitterY = (haltonY * 2.0f - 1.0f) / (float)SCR_HEIGHT;
            
            jitterU = jitterX * 0.5f;
            jitterV = jitterY * 0.5f;

            jitterProj[2][0] += jitterX;
            jitterProj[2][1] += jitterY;
        }

        // ==========================================
        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(jitterProj));

        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(cleanView));

        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * 2, sizeof(glm::mat4), glm::value_ptr(cleanProj));

        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * 3, sizeof(glm::mat4), glm::value_ptr(prevProj));

        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * 4, sizeof(glm::mat4), glm::value_ptr(prevView));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // ===================================================
        // 🚀 [新增] 每一帧的绝对开头：启动 Compute Shader 算物理
        // ===================================================
        rainComputeShader.use();

        rainComputeShader.setFloat("deltaTime", deltaTime);
        rainComputeShader.setFloat("time", currentFrame);
        rainComputeShader.setVec3("cameraPos", camera.Position);

        // 绑定 G-Buffer 世界坐标贴图
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gPosition);
        rainComputeShader.setInt("gPositionMap", 0);

        // 🌟 [新增] 绑定 G-Buffer 法线贴图，让水滴知道怎么反弹！
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gNormal);
        rainComputeShader.setInt("gNormalMap", 1);

        glDispatchCompute(NUM_PARTICLES / 256 + 1, 1, 1);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // 🌟 Pass 1: 定向光阴影贴图 (CSM Shadow Pass)
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
        glClearColor(0.01f, 0.01f, 0.01f, 1.0f);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        csmShadowDepthShader.use();

        rainyAlley.Draw(csmShadowDepthShader, camera, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

        // 🌟 Pass 2: 几何阶段 (G-Buffer Geometry Pass)
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glDepthMask(GL_TRUE);   // Masked 物体必须写深度！
        glDisable(GL_BLEND);    // Masked 不需要混合，用 discard 处理镂空

        gBufferShader.use();

        gBufferShader.setMat4("jitterProj", jitterProj);

        gBufferShader.setFloat("height_scale", 0.05f);
        gBufferShader.setBool("useParallax", false);

        // 🌟 [ImGui 替换] 使用滑动条控制全局湿度
        gBufferShader.setFloat("u_GlobalWetness", ui_GlobalWetness);

        gBufferShader.setFloat("u_Time", currentFrame);

        gBufferShader.setFloat("u_DebugEmissiveUV", u_DebugEmissiveUV);

        // 核心修复：在调用 rainyAlley.DrawOpaque 之前，激活高位槽并绑定噪音图！
        // glActiveTexture(GL_TEXTURE15);
        // glBindTexture(GL_TEXTURE_2D, puddleNoiseTexture); // 确保这个变量名对应你加载的噪音贴图 ID

        // 🌟 [关键修改 1]：在 G-Buffer 阶段只画不透明网格（墙体、地面、道具）
        rainyAlley.DrawOpaque(gBufferShader, camera, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

        gBufferShader.setBool("isMasked", false); // 恢复默认

        // 🌟 Pass 2.1: 计算 SSAO 阶段
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST); 

        ssaoShader.use();

        ssaoShader.setFloat("radius", 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gPosition);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gNormal);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, noiseTexture);

        renderScreenQuad();

        // 🌟 Pass 2.2: SSAO 模糊阶段
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        ssaoBlurShader.use();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer); // 上一步生成的满是噪点的 SSAO 图

        renderScreenQuad();

        // 🌟 Pass 3: 光照阶段 (Deferred Lighting Pass)
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        deferredPurePBRShader.use();

        // 绑定 G-Buffer 纹理全家桶
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gPosition);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gNormal);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gAlbedo_parallaxShadow);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, gORM);

        glm::mat4 model = glm::mat4(1.0f);
        deferredPurePBRShader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        deferredPurePBRShader.setBool("blinn", blinn);

        deferredPurePBRShader.setVec3("viewPos", camera.Position);
        deferredPurePBRShader.setVec3("lightDir", lightDir);
        
        // 1. 压暗主光：让夜晚降临！不要大太阳了，换成幽暗的月光
        // glm::vec3 moonlightColor = glm::vec3(0.05f, 0.1f, 0.2f); // 暗蓝色
        glm::vec3 moonlightColor = glm::vec3(0.5f, 0.5f, 0.5f);
        deferredPurePBRShader.setVec3("lightColor", moonlightColor);

        deferredPurePBRShader.setBool("u_DebugShowEmissive", false); // 设为 true 即开启排查模式

        // ==============================================================
        // 🌟🌟🌟 第二步：将采集到的点光源坐标和颜色传给 Shader 🌟🌟🌟
        // ==============================================================
        // 告诉 Shader 当前一共有多少盏灯
        deferredPurePBRShader.setInt("activePointLightsCount", static_cast<int>(lightData.size()));

        deferredPurePBRShader.setInt("cascadeCount", static_cast<int>(shadowCascadeLevels.size()));
        for (size_t i = 0; i < shadowCascadeLevels.size(); ++i)
        {
            deferredPurePBRShader.setFloat("cascadePlaneDistances[" + std::to_string(i) + "]", shadowCascadeLevels[i]);
        }

        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);

        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_2D_ARRAY, lightDepthMaps);

        glActiveTexture(GL_TEXTURE11);
        glBindTexture(GL_TEXTURE_2D, ssaoColorBufferBlur);

        renderScreenQuad();

        // ==========================================
        // 🌟 [ImGui 替换] Pass 3.5: 全局体积光 (月光神明之光)
        // ==========================================
        if (ui_EnableVolumetric)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO); // 继续画在 HDR 缓冲上

            // 🌟 开启加法混合！把算出来的光柱直接“叠”在建筑表面上
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDisable(GL_DEPTH_TEST); // 全屏后处理，关深度

            volumetricLightShader.use();

            // 1. 绑 G-Buffer 拿到世界坐标和法线
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gPosition);
            volumetricLightShader.setInt("gPosition", 0);

            // 2. 绑 CSM 阴影阵列
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D_ARRAY, lightDepthMaps);
            volumetricLightShader.setInt("shadowMap", 10);

            // 3. 传参：摄像机位置，月光方向，月光颜色
            volumetricLightShader.setVec3("viewPos", camera.Position);
            volumetricLightShader.setVec3("lightDir", lightDir);
            // C++ 端：把月光体积光压榨到极限的暗，让它只有微弱的丁达尔效应
            volumetricLightShader.setVec3("lightColor", glm::vec3(0.05f, 0.08f, 0.12f)); 

            // 4. 传 CSM 的级联切割参数
            volumetricLightShader.setInt("cascadeCount", static_cast<int>(shadowCascadeLevels.size()));
            for (size_t i = 0; i < shadowCascadeLevels.size(); ++i) {
                volumetricLightShader.setFloat("cascadePlaneDistances[" + std::to_string(i) + "]", shadowCascadeLevels[i]);
            }

            volumetricLightShader.setInt("activePointLightsCount", static_cast<int>(lightData.size()));

            renderScreenQuad();

            // 画完之后立刻关闭混合，恢复深度测试，防止弄坏后面的渲染！
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
        }

        // 🌟 Pass 4: 深度拷贝 (Depth Blit) - 极其重要的一步！
        // 因为前面的光照阶段是在 2D 矩形上画的，hdrFBO 里没有场景的深度信息。
        // 如果现在直接画天空盒和玻璃，它们会覆盖掉前面的物体。
        // 所以要把 gBuffer 的深度强行“复制”给 hdrFBO。
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdrFBO);
        glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        // =================================================================
        // 🌟🌟🌟 [全新插入] Pass 4.5: 前向半透明玻璃 Pass (Forward Transparent Pass) 🌟🌟🌟
        // =================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO); // 绘制目标为 HDR 颜色缓冲
        
        glDisable(GL_CULL_FACE); // 确保双面都能看到玻璃
        glEnable(GL_BLEND);
        // 🌟 必须使用标准 Alpha 混合
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);                            
        glDepthMask(GL_FALSE); // ⚠️ 玻璃不写深度！

        transparentForwardShader.use();
        transparentForwardShader.setVec3("viewPos", camera.Position);
        transparentForwardShader.setVec3("lightDir", lightDir);
        transparentForwardShader.setVec3("lightColor", glm::vec3(0.05f, 0.08f, 0.12f));

        // 🚀 [修复 1]: 统一命名！与 Shader 中的 "glassAlpha" 保持一致！
        transparentForwardShader.setFloat("glassAlpha", 0.4f); 

        // 🚀 [修复 2]: 必须激活玻璃逻辑，否则 Shader 把它当实体墙！
        transparentForwardShader.setBool("isGlassMaterial", true);

        // 3. 绘制半透明网格
        rainyAlley.DrawTransparent(transparentForwardShader, camera, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

        // 4. 恢复 OpenGL 渲染状态
        glDepthMask(GL_TRUE);  
        glDisable(GL_BLEND);

        // 🌟 Pass 5: 前向渲染阶段 (Forward Pass)
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glEnable(GL_DEPTH_TEST); // 恢复深度测试

        // 必须开启，否则灯泡会透视穿墙！
        glDepthFunc(GL_LEQUAL);

        shiner.use();

        // 💡 1. 遍历全新的 lightData 数组，画出 21 个物理灯泡！
        for (size_t i = 0; i < lightData.size(); ++i)
        {
            glm::mat4 lightModel = glm::mat4(1.0f);
            
            // 从 vec4 中解包出 vec3 的坐标
            glm::vec3 pos = glm::vec3(lightData[i].Position);   
            lightModel = glm::translate(lightModel, pos);
            lightModel = glm::scale(lightModel, glm::vec3(0.09f));

            shiner.setMat4("model", lightModel);
            
            // 🌟 核心：取出基础颜色，并乘以它的物理强度 (w分量) 让灯泡极度亮眼！
            glm::vec3 col = glm::vec3(lightData[i].Color);
            float intensity = lightData[i].Color.w;
            // 控制一下灯泡自身的白化程度，保留赛博色彩
            shiner.setVec3("lightColor", col * (intensity * 10.0f)); 

            // 🔪🔪🔪 就在这里！注释掉！杀掉这个破坏气氛的白球！
            // renderSphere();
        }

        // 画月亮 (太阳)
        glm::mat4 sunModel = glm::mat4(1.0f);
        sunModel = glm::translate(sunModel, camera.Position + lightDir * 500.0f); // 确保在天空
        sunModel = glm::scale(sunModel, glm::vec3(10.0f)); // 稍微放大点
        shiner.setMat4("model", sunModel);
        shiner.setVec3("lightColor", glm::vec3(2.0, 3.0, 4.0)); // 偏冷的月光色
        renderSphere();

        // 🌟 Pass 5.5: [ImGui 替换] 屏幕空间反射 (SSR) 阶段
        if (ui_EnableSSR)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, hdrFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

            glBindTexture(GL_TEXTURE_2D, hdrColorCopyTexture);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, SCR_WIDTH, SCR_HEIGHT);
            glGenerateMipmap(GL_TEXTURE_2D); // 🔥 核心魔法：为粗糙材质准备的物理模糊！

            glBindFramebuffer(GL_FRAMEBUFFER, ssrTraceFBO);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);

            ssrRayMarchingTraceShader.use();

            // 绑定 G-Buffer
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gPosition);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gNormal);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gORM); // 需要粗糙度来决定步进精度(优化)

            renderScreenQuad();

            glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);

            ssrCompositeShader.use();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ssrTraceTexture); // 刚算出的 SSR 字典
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, hdrColorCopyTexture); // 带有 Mipmap 的全屏 HDR 备份
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gPosition);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gNormal);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, gORM); // PBR 粗糙度
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, gAlbedo_parallaxShadow);

            glDisable(GL_BLEND); // 🔪 关掉它！我们要直接覆盖原像素！

            renderScreenQuad();
        }

        // =================================================================
        // 🌟🌟🌟 [全新插入] Pass 5.8: TAA 时间抗锯齿 Resolve 阶段 🌟🌟🌟
        // =================================================================
        
        // 1. 冷启动保护：如果是第一帧，直接把 hdrFBO 的当前完美画面，原样拷贝给历史缓冲
        if (!isTAAInitialized)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, hdrFBO);
            
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, taaReadFBO);
            glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, taaWriteFBO);
            glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

            isTAAInitialized = true;
        }

        // 2. 正式执行 TAA 混合
        glBindFramebuffer(GL_FRAMEBUFFER, taaWriteFBO); // 目标是当前的写缓冲
        glDisable(GL_DEPTH_TEST);
        
        // 这里的 taaResolveShader 是你马上要写的那个 Shader (见指南第三关)
        taaResolveShader.use();

        // ==========================================
        // 🌟 修正：向 Shader 传递当前帧和上一帧的抖动！
        // ==========================================

        // 🌟 仅仅保留这一行！
        taaResolveShader.setVec2("u_JitterUV", glm::vec2(jitterU, jitterV));

        glActiveTexture(GL_TEXTURE0); 
        glBindTexture(GL_TEXTURE_2D, screenTexture[0]); // hdrFBO 里当前刚画完的、带有狗牙的图
        taaResolveShader.setInt("currentFrameTex", 0);

        glActiveTexture(GL_TEXTURE1); 
        glBindTexture(GL_TEXTURE_2D, taaReadTex);       // 上一帧抗好锯齿的历史图
        taaResolveShader.setInt("historyFrameTex", 1);

        glActiveTexture(GL_TEXTURE2); 
        glBindTexture(GL_TEXTURE_2D, gVelocity);        // 第二关画出来的速度图
        taaResolveShader.setInt("velocityTex", 2);

        renderScreenQuad(); // 执行时空融合！

        // ===================================================
        // 🌧️ [管线重组] 终极视觉：在 TAA 融合完毕后，再降下暴雨！
        // ===================================================
        // 目标：将雨滴直接画在已经抗锯齿完毕的 taaWriteFBO 上！
        glBindFramebuffer(GL_FRAMEBUFFER, taaWriteFBO);
        
        // 🌟 核心魔法 (借用深度)：taaWriteFBO 只有颜色图，没有深度图。
        // 为了让雨滴依然能被大楼遮挡（不穿墙），我们把 G-Buffer 的深度缓冲借给它用一下！
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gRbo);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE); // 雨滴是半透明的，不写入深度
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); 

        // 🌟 1. 计算当前帧相机在世界空间中的移动速度
        static glm::vec3 lastCameraPos = camera.Position;
        glm::vec3 cameraVelocity = (camera.Position - lastCameraPos) / deltaTime;
        lastCameraPos = camera.Position; // 更新缓存

        // 🌟 2. 绑定贴图与 Uniforms
        rainShader.use();
        rainShader.setFloat("time", currentFrame);
        rainShader.setVec3("cameraPos", camera.Position);
    
        // 传入屏幕尺寸（用于重构像素深度 UV）
        rainShader.setVec2("screenSize", glm::vec2(SCR_WIDTH, SCR_HEIGHT));

        // 🌟 3. 传入 G-Buffer 深度和位置，用于雨丝的 Pixel-Perfect 剔除与过渡
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gPosition); // 你的 G-Buffer 世界位置纹理
        rainShader.setInt("gPositionMap", 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, gNormal);   // 你的 G-Buffer 世界法线纹理
        rainShader.setInt("gNormalMap", 3);

        // 🌟 4. 执行绘制
        glBindVertexArray(emptyVAO);
        glEnable(GL_BLEND);
        // 🚨 终极核武器：预乘 Alpha 混合！(Premultiplied Alpha)
        // 这一步能让雨滴高光极其明亮(GL_ONE)，同时几乎不遮挡背景(GL_ONE_MINUS_SRC_ALPHA)，彻底消除发白雾霾！
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glDrawArrays(GL_TRIANGLES, 0, NUM_PARTICLES * 6);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE); 
        
        // 🌟 物归原主：画完雨之后，必须解除深度绑定，保持 taaWriteFBO 纯净！
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);

        // 🌟 Pass 6: 后期处理阶段 (Post-Processing)
        glBindFramebuffer(GL_FRAMEBUFFER, 0); // 回到默认屏幕缓冲

        if (ui_EnableBloom) 
        {
            // 🚨🚨🚨 【高危注意：数据源变更】 🚨🚨🚨
            // 我们不能再拿 screenTexture[0] 去做 Bloom 了！
            // 必须拿刚刚被 TAA 盘得无比丝滑的 taaWriteTex 去做 Bloom！
            // 0.005f 是 filterRadius，可以自己在代码里调整大小看效果
            bloomRenderer.RenderBloomTexture(taaWriteTex, 0.005f); 
        }

        glDisable(GL_DEPTH_TEST);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

        if (showDebugDepth)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            // --- 🎨 正常后期处理 (ToneMapping + Gamma + Bloom合成) ---
            // 目标：渲染到我们刚才新建的 postProcessFBO 里
            glBindFramebuffer(GL_FRAMEBUFFER, postProcessFBO); 
            // glClearColor(0.02f, 0.02f, 0.05f, 1.0f); // 给一个极暗的夜空色
            glClearColor(0.2f, 0.2f, 0.5f, 1.0f); // 给一个极暗的夜空色

            glClear(GL_COLOR_BUFFER_BIT);

            postProcessShader.use();
            postProcessShader.setFloat("offset_x", 1.0f / SCR_WIDTH);
            postProcessShader.setFloat("offset_y", 1.0f / SCR_HEIGHT);

            // 🌟 [ImGui 替换] 曝光度和 Bloom 开关
            postProcessShader.setFloat("exposure", ui_Exposure);
            postProcessShader.setBool("bloom", ui_EnableBloom);

            glActiveTexture(GL_TEXTURE0);
            // 同样，调色 Shader 读取的底图，也必须从 screenTexture 换成 taaWriteTex！
            glBindTexture(GL_TEXTURE_2D, taaWriteTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, bloom ? bloomRenderer.BloomTexture() : 0); // 泛光
            Lens_Dirt.bind(2); // 镜头污渍

            // 🌟 重新加回：计算 3D 坐标到 2D 屏幕的投影，用于传递给 Shader 绘制正六边形
            std::vector<glm::vec2> lightScreenPositions;
            std::vector<glm::vec3> lightScreenColors;
            
            // 遍历所有路灯和霓虹灯
            for (size_t i = 0; i < lightData.size(); ++i) {
                glm::vec3 pos = glm::vec3(lightData[i].Position);
                glm::vec4 clip = cleanProj * cleanView * glm::vec4(pos, 1.0f);
                if (clip.z > 0) { // 只算在相机前方的
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    lightScreenPositions.push_back(glm::vec2(ndc) * 0.5f + 0.5f);
                    lightScreenColors.push_back(glm::vec3(lightData[i].Color));
                }
            }

            // 把月亮也塞进去
            glm::vec4 sunClip = cleanProj * cleanView * glm::vec4(camera.Position - lightDir * 500.0f, 1.0f);
            if (sunClip.z > 0) {
                glm::vec3 sunNDC = glm::vec3(sunClip) / sunClip.w;
                lightScreenPositions.push_back(glm::vec2(sunNDC) * 0.5f + 0.5f);
                lightScreenColors.push_back(glm::vec3(1.0, 1.5, 2.0));
            }

            // 送入 Shader
            postProcessShader.setInt("numLights", (int)lightScreenPositions.size());
            for (int i = 0; i < lightScreenPositions.size(); ++i) {
                // 如果你的 Shader 里数组没改大，限制一下防止越界崩溃！
                if (i >= 32) break; 
                postProcessShader.setVec2("lightsPos[" + std::to_string(i) + "]", lightScreenPositions[i]);
                postProcessShader.setVec3("lightsColor[" + std::to_string(i) + "]", lightScreenColors[i]);
            }

            renderScreenQuad(); // 画出带炫酷光晕的画面

            // --- 🖥️ 屏幕输出阶段 (FXAA 抗锯齿上屏) ---
            // 目标：回到默认的电脑屏幕缓冲 (0)
            glBindFramebuffer(GL_FRAMEBUFFER, 0); 
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            screenShader.use(); 
            // 传给 FXAA 计算像素偏移用的
            screenShader.setFloat("offset_x", 1.0f / SCR_WIDTH); 
            screenShader.setFloat("offset_y", 1.0f / SCR_HEIGHT);

            glActiveTexture(GL_TEXTURE0);
            // ⚠️ 关键点：这里绑定的不再是 HDR 场景，而是上一步生成的 postProcessTexture！
            glBindTexture(GL_TEXTURE_2D, postProcessTexture); 

            renderScreenQuad(); // 最后一次绘制，完美丝滑无锯齿的画面上屏！
        }

        // =======================================================
        // 🌟 [ImGui 新增] 第三步：渲染 ImGui 面板
        // =======================================================
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        prevProj = cleanProj;
        prevView = cleanView;

        // 🌟 留给下一帧用
        prevJitterU = jitterU;
        prevJitterV = jitterV;

        std::swap(taaReadFBO, taaWriteFBO);
        std::swap(taaReadTex, taaWriteTex);

        glEnable(GL_DEPTH_TEST);

        fpsCounter.update(window);

        // 交换缓冲 (SwapBuffers)
        glfwSwapBuffers(window);
        // 处理事件 (PollEvents)
        glfwPollEvents();
    }
    // 释放资源 (Delete VAO/VBO/Program)

    // 结束 GLFW
    glfwTerminate();

    return 0;
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

        static bool tabPressed = false;

        if(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS)
        {
            if(!tabPressed)
            {
                isMouseCaptured = !isMouseCaptured;
                if(isMouseCaptured)
                {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
                else
                {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                tabPressed = true;
            }
        }
        else
        {
            tabPressed = false; // 松开按键后，解锁
        }

        static bool f11Pressed = false;

        if(glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS)
        {
            if(!f11Pressed)
            {
                isFullscreen = !isFullscreen;

                if(isFullscreen)
                {
                    // --- 切换到全屏 ---
                    glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
                    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

                    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();

                    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

                    glfwSetWindowMonitor(window, primaryMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                }
                else
                {
                    glfwSetWindowMonitor(window, NULL, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
                }
                f11Pressed = true;
            }
        }
        else
        {
            f11Pressed = false;
        }

    if (isMouseCaptured)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);
    }

    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS && !blinnKeyPressed) 
    {
        blinn = !blinn;
        blinnKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_RELEASE) 
    {
        blinnKeyPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS && !showDebugDepthKeyPressed) 
    {
        showDebugDepth = !showDebugDepth;
        showDebugDepthKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_RELEASE) 
    {
        showDebugDepthKeyPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && !bloomKeyPressed) 
    {
        bloom = !bloom;
        bloomKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE) 
    {
        bloomKeyPressed = false;
    }

    // 💡 找坐标外挂：按下 P 键，在控制台打印当前摄像机的位置
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS)
    {
        if (!pKeyPressed) // 防抖
        {
            std::cout << "推荐光源坐标: glm::vec3(" 
                        << camera.Position.x << "f, " 
                        << camera.Position.y << "f, " 
                        << camera.Position.z << "f);" << std::endl;
            pKeyPressed = true;
        }
    }
    else if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE)
    {
        pKeyPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !u_DebugEmissiveUVKeyPressed) 
    {
        u_DebugEmissiveUV = !u_DebugEmissiveUV;
        u_DebugEmissiveUVKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) 
    {
        u_DebugEmissiveUVKeyPressed = false;
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // 防止窗口最小化时 width 和 height 为 0 导致 OpenGL 崩溃
    if (width == 0 || height == 0) return;

    SCR_WIDTH = width;
    SCR_HEIGHT = height;

    // 1. 基础操作：调整 OpenGL 视口
    glViewport(0, 0, width, height);

    // ==========================================
    // 2. 重置所有 G-Buffer 纹理大小
    // ==========================================
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, gAlbedo_parallaxShadow);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    // 🌟 这里注意：因为你在 G-Buffer Shader 里改成了 vec4 输出，
    // 原来的 GL_RGB 必须改为 GL_RGBA！
    glBindTexture(GL_TEXTURE_2D, gORM);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindTexture(GL_TEXTURE_2D, gVelocity);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, NULL);

    glBindRenderbuffer(GL_RENDERBUFFER, gRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);

    // ==========================================
    // 3. 重置 HDR 前向渲染 FBO 纹理与深度 RBO
    // ==========================================
    // 🔥 修复：之前这里有 for(i<2) 的越界 bug，因为你只创建了 screenTexture[0]
    glBindTexture(GL_TEXTURE_2D, screenTexture[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);

    glBindRenderbuffer(GL_RENDERBUFFER, hdrRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    // ==========================================
    // 4. 重置 SSAO 纹理大小 (🔥 新增，解决全屏错位)
    // ==========================================
    glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RGB, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, ssaoColorBufferBlur);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RGB, GL_FLOAT, NULL);

    // ==========================================
    // 5. 重置 PostProcess 最终输出缓冲 (🔥 新增，解决后处理错位)
    // ==========================================
    glBindTexture(GL_TEXTURE_2D, postProcessTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    // ==========================================
    // 6. 重置 Bloom 渲染器
    // ==========================================
    bloomRenderer.Init(width, height);

    // ==========================================
    // 7. 🌟 重点新增：重置 SSR 相关纹理
    // ==========================================
    glBindTexture(GL_TEXTURE_2D, ssrTraceTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, hdrColorCopyTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);

    // ==========================================
    // 8. 🌟 重置 TAA Ping-Pong 历史缓冲大小
    // ==========================================
    glBindTexture(GL_TEXTURE_2D, taaTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    
    glBindTexture(GL_TEXTURE_2D, taaTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

    // 解绑，保持状态干净
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    std::cout << "Window Resized: All FBOs successfully updated to " << width << "x" << height << std::endl;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if(!isMouseCaptured) return;

    if(firstMouse)
    {
        lastY = static_cast<float>(ypos);
        lastX = static_cast<float>(xpos);
        firstMouse = false;
    }
    float yoffset = lastY - static_cast<float>(ypos);
    float xoffset = static_cast<float>(xpos) - lastX;
    lastY = static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void renderCube()
{
    static unsigned int cubeVAO = 0;
    static unsigned int cubeVBO = 0;
    // initialize (if necessary)
    if (cubeVAO == 0)
    {
                float vertices[] = {
            // Pos                  // Normal           // Tex      // Tangent           // Bitangent
            // Back face
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // bottom-left
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // top-right
             1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // bottom-right         
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // top-right
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // bottom-left
            -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // top-left
            // Front face
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
             1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            -1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            // Left face
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            // Right face
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            // Bottom face
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            // Top face
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
             1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
             1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
             1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        // fill buffer
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        // link vertex attributes
        glBindVertexArray(cubeVAO);
        
        // 1. Pos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)0);
        // 2. Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(3 * sizeof(float)));
        // 3. TexCoords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(6 * sizeof(float)));
        // 4. Tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(8 * sizeof(float)));
        // 5. Bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(11 * sizeof(float)));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    // render Cube
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void renderScreenQuad()
{
    static unsigned int screenQuadVAO = 0;
    if (screenQuadVAO == 0)
    {
        // setup plane VAO
        glGenVertexArrays(1, &screenQuadVAO);
    }
    glBindVertexArray(screenQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void renderNormalMappedQuad()
{
    if (normalMapVAO == 0)
    {
        // positions
        glm::vec3 pos1(-1.0, 1.0, 0.0);
        glm::vec3 pos2(-1.0, -1.0, 0.0);
        glm::vec3 pos3(1.0, -1.0, 0.0);
        glm::vec3 pos4(1.0, 1.0, 0.0);
        // texture coordinates
        glm::vec2 uv1(0.0, 1.0);
        glm::vec2 uv2(0.0, 0.0);
        glm::vec2 uv3(1.0, 0.0);
        glm::vec2 uv4(1.0, 1.0);
        // normal vector
        glm::vec3 nm(0.0, 0.0, 1.0);

        // calculate tangent/bitangent vectors of both triangles
        glm::vec3 tangent1, bitangent1;
        glm::vec3 tangent2, bitangent2;
        // - triangle 1
        glm::vec3 edge1 = pos2 - pos1;
        glm::vec3 edge2 = pos3 - pos1;
        glm::vec2 deltaUV1 = uv2 - uv1;
        glm::vec2 deltaUV2 = uv3 - uv1;

        GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        tangent1 = glm::normalize(tangent1);

        bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        bitangent1 = glm::normalize(bitangent1);

        // - triangle 2
        edge1 = pos3 - pos1;
        edge2 = pos4 - pos1;
        deltaUV1 = uv3 - uv1;
        deltaUV2 = uv4 - uv1;

        f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        tangent2 = glm::normalize(tangent2);


        bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        bitangent2 = glm::normalize(bitangent2);


        GLfloat quadVertices[] = {
            // Positions            // normal         // TexCoords  // Tangent                          // Bitangent
            pos1.x, pos1.y, pos1.z, nm.x, nm.y, nm.z, uv1.x, uv1.y, tangent1.x, tangent1.y, tangent1.z, bitangent1.x, bitangent1.y, bitangent1.z,
            pos2.x, pos2.y, pos2.z, nm.x, nm.y, nm.z, uv2.x, uv2.y, tangent1.x, tangent1.y, tangent1.z, bitangent1.x, bitangent1.y, bitangent1.z,
            pos3.x, pos3.y, pos3.z, nm.x, nm.y, nm.z, uv3.x, uv3.y, tangent1.x, tangent1.y, tangent1.z, bitangent1.x, bitangent1.y, bitangent1.z,

            pos1.x, pos1.y, pos1.z, nm.x, nm.y, nm.z, uv1.x, uv1.y, tangent2.x, tangent2.y, tangent2.z, bitangent2.x, bitangent2.y, bitangent2.z,
            pos3.x, pos3.y, pos3.z, nm.x, nm.y, nm.z, uv3.x, uv3.y, tangent2.x, tangent2.y, tangent2.z, bitangent2.x, bitangent2.y, bitangent2.z,
            pos4.x, pos4.y, pos4.z, nm.x, nm.y, nm.z, uv4.x, uv4.y, tangent2.x, tangent2.y, tangent2.z, bitangent2.x, bitangent2.y, bitangent2.z
        };
        // Setup plane VAO
        glGenVertexArrays(1, &normalMapVAO);
        glGenBuffers(1, &normalMapVBO);
        glBindVertexArray(normalMapVAO);
        glBindBuffer(GL_ARRAY_BUFFER, normalMapVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(8 * sizeof(GLfloat)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(11 * sizeof(GLfloat)));
    }
    glBindVertexArray(normalMapVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& projview)
{
    const auto inv = glm::inverse(projview);

    std::vector<glm::vec4> frustumCorners;
    for(unsigned int x = 0; x < 2; x++)
    {
        for(unsigned int y = 0; y < 2; y++)
        {
            for(unsigned int z = 0; z < 2; z++)
            {
                const glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }
    return frustumCorners;
}

std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view)
{
    return getFrustumCornersWorldSpace(proj * view);
}

glm::mat4 getLightSpaceMatrix(const float& nearPlane, const float& farPlane)
{
    const auto projection = glm::perspective(glm::radians(camera.Zoom), static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT), nearPlane, farPlane);
    const auto view = camera.GetViewMatrix();
    const auto corners = getFrustumCornersWorldSpace(projection, view);

    glm::vec3 center = glm::vec3(0.0f);
    for(const auto& v : corners)
    {
        center += glm::vec3(v.x, v.y, v.z);
    }
    center /= static_cast<float>(corners.size());

    const auto lightView = glm::lookAt(center + lightDir, center, glm::vec3(0.0f, 1.0f, 0.0f));

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for(const auto& v : corners)
    {
        const auto trf = lightView * v;
        minX = std::min(minX, trf.x);
        maxX = std::max(maxX, trf.x);
        minY = std::min(minY, trf.y);
        maxY = std::max(maxY, trf.y);
        minZ = std::min(minZ, trf.z);
        maxZ = std::max(maxZ, trf.z);
    }

    float zMargin = 100.0f;
    minZ -= zMargin; 
    maxZ += zMargin * 0.5f;

    float sideMargin  = (maxX - minX) / 4096; 

    minX = std::floor(minX / sideMargin) * sideMargin;
    maxX = std::floor(maxX / sideMargin) * sideMargin;
    minY = std::floor(minY / sideMargin) * sideMargin;
    maxY = std::floor(maxY / sideMargin) * sideMargin;

    const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

    return lightProjection * lightView;
}

std::vector<glm::mat4> getLightSpaceMatrices()
{
    std::vector<glm::mat4> ret;
    for(size_t i = 0; i < shadowCascadeLevels.size() + 1; i++)
    {
        if(i == 0)
        {
            ret.push_back(getLightSpaceMatrix(cameraNearPlane, shadowCascadeLevels[i]));
        }
        else if(i < shadowCascadeLevels.size())
        {
            ret.push_back(getLightSpaceMatrix(shadowCascadeLevels[i - 1], shadowCascadeLevels[i]));
        }
        else
        {
            ret.push_back(getLightSpaceMatrix(shadowCascadeLevels[i - 1], cameraFarPlane));
        }
    }
    return ret;
}

void renderSphere()
{
    static unsigned int sphereVAO = 0;
    static unsigned int indexCount = 0;

    if(sphereVAO == 0)
    {
        glGenVertexArrays(1, &sphereVAO);

        unsigned int sphereVBO;
        unsigned int sphereEBO;

        glGenBuffers(1, &sphereVBO);
        glGenBuffers(1, &sphereEBO);

        std::vector<glm::vec3> position;
        std::vector<glm::vec2> uv;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec3> tangents;
        std::vector<glm::vec3> bitangents;
        std::vector<unsigned int> indices;

        const unsigned int X_SEGMENTS = 64;
        const unsigned int Y_SEGMENTS = 64;
        const float PI = 3.14159265359f;

        for(unsigned int x = 0; x <= X_SEGMENTS; x++)
        {
            for(unsigned int y = 0; y <= Y_SEGMENTS; y++)
            {
                float xSegment = static_cast<float>(x) / static_cast<float>(X_SEGMENTS);
                float ySegment = static_cast<float>(y) / static_cast<float>(Y_SEGMENTS);

                float yPos = std::cos(ySegment * PI);
                float sliceRadius = std::sin(ySegment * PI);

                float xPos = std::cos(xSegment * 2.0f * PI) * sliceRadius;
                float zPos = std::sin(xSegment * 2.0f * PI) * sliceRadius;

                // U方向的导数 (切线)
                glm::vec3 tangent(
                    -std::sin(xSegment * 2.0f * PI), 
                    0.0f, 
                    std::cos(xSegment * 2.0f * PI)
                );
                
                // V方向的导数 (副切线)
                glm::vec3 bitangent(
                    std::cos(xSegment * 2.0f * PI) * std::cos(ySegment * PI),
                    -std::sin(ySegment * PI),
                    std::sin(xSegment * 2.0f * PI) * std::cos(ySegment * PI)
                );

                position.push_back(glm::vec3(xPos, yPos, zPos));
                uv.push_back(glm::vec2(xSegment, ySegment));
                normals.push_back(glm::vec3(xPos, yPos, zPos));
                tangents.push_back(tangent);
                bitangents.push_back(bitangent);
            }
        }

        bool oddRow = false;
        for(unsigned int y = 0; y < Y_SEGMENTS; y++)
        {
            if(!oddRow)
            {
                for(unsigned int x = 0; x <= X_SEGMENTS; x++)
                {
                    indices.push_back(y * (X_SEGMENTS + 1) + x);
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                }
            }
            else
            {
                for(int x = X_SEGMENTS; x >= 0; x--)
                {
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                    indices.push_back(y * (X_SEGMENTS + 1) + x);
                }
            }
            oddRow = !oddRow;
        }
        indexCount = static_cast<unsigned int>(indices.size());

        std::vector<float> data;
        for(unsigned int i = 0; i < position.size(); i++)
        {
            data.push_back(position[i].x);
            data.push_back(position[i].y);
            data.push_back(position[i].z);

            if(normals.size() > 0)
            {
                data.push_back(normals[i].x);
                data.push_back(normals[i].y);
                data.push_back(normals[i].z);
            }

            if(uv.size() > 0)
            {
                data.push_back(uv[i].x);
                data.push_back(uv[i].y);
            }

            data.push_back(tangents[i].x);
            data.push_back(tangents[i].y);
            data.push_back(tangents[i].z);

            data.push_back(bitangents[i].x);
            data.push_back(bitangents[i].y);
            data.push_back(bitangents[i].z);
        }
        glBindVertexArray(sphereVAO);

        glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(11 * sizeof(float)));
    }
    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLE_STRIP, indexCount, GL_UNSIGNED_INT, 0);
}

float lerp(float a, float b, float f)
{
    return a + f * (b - a);
}

// 🌟 把它放在 main 函数外面
// 参数 index: 当前是第几个采样点 (注意：必须从 1 开始，不能是 0)
// 参数 base: 质数基底 (X轴填2，Y轴填3)
float GetHaltonValue(int index, int base)
{
    float fraction = 1.0f;
    float result = 0.0f;
    int currentIndex = index;
    
    while (currentIndex > 0) 
    {
        fraction /= static_cast<float>(base);
        result += static_cast<float>(currentIndex % base) * fraction;
        currentIndex /= base;
    }
    return result;
}