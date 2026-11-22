#pragma once

#ifndef GLM_FORCE_ALIGNED_GENTYPES
#define GLM_FORCE_ALIGNED_GENTYPES
#endif

#include <cstddef>
#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/type_aligned.hpp>


// Vertex structs
// --------------
struct FVertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoord;
};

struct FInstanceData
{
    glm::mat4x4 Model{ glm::mat4x4(1.0f) };
};

struct FSkyboxVertex
{
    glm::vec3 Position;
};

struct FQuadVertex
{
    glm::vec2 Position;
    glm::vec2 TexCoord;
};

struct FQuadOnlyVertex
{
    glm::vec2 Position;
};

// Uniform buffer structs
// ----------------------
struct FGameArgs
{
    glm::aligned_vec2 Resolution;
    float FovRadians;
    float Time;
    float GameTime;
    float TimeDelta;
    float TimeRate;
};

struct FBlackHoleArgs
{
    glm::mat4x4 InverseCamRot;
    glm::vec4 BlackHoleRelativePosRs;
    glm::vec4 BlackHoleRelativeDiskNormal;
    glm::vec4 BlackHoleRelativeDiskTangen;
    glm::vec4 CameraVelocity;
    glm::vec4 ie1_up;                  // 四维相机数据
    glm::vec4 ie2_up;
    glm::vec4 ie3_up;
    glm::vec4 iU_up;
    int   iCamDataCoordisOutgoing;     // 1为outgoing, 0为ingoing
    int   DEBUG;
    int   Prepass;
    int   Whitehole;
    int   InWhichUniverse;
    int   Grid;
    int   EnableHeatHaze;
    int   EnableShadowCulling;
    int   ObserverMode;
    int   Polarization;
    int   UseImageDisk;
    float Quality;
    float UniverseSign;
    float BlackHoleTime;
    float BlackHoleMassSol;
    float Spin;
    float Q2;
    float Mu;
    float AccretionRate;
    float BackShiftMax;
    float DensestarsurfaceR;                     
    float DensestarBlackbodyIntensityExponent;   
    float DensestarRedShiftColorExponent;        
    float DensestarRedShiftIntensityExponent;    
    float DensestarBrightmut;
    float InterRadiusRs;
    float OuterRadiusRs;
    float ThinRs;
    float Hopper;
    float Brightmut;
    float Darkmut;
    float Reddening;
    float Saturation;
    float BlackbodyIntensityExponent;
    float RedShiftColorExponent;
    float RedShiftIntensityExponent;
    float ImageRotationSpeed;
    float PolarizationAngle;
    float HeatHaze;
    float BackgroundBrightmut;
    float PhotonRingBoost;
    float PhotonRingColorTempBoost;
    float BoostRot;
    float JetRedShiftIntensityExponent;
    float JetBrightmut;
    float JetSaturation;
    float JetShiftMax;
    float BlendWeight;
};

struct FMatrices
{
    glm::aligned_mat4x4 View{ glm::mat4x4(1.0f) };
    glm::aligned_mat4x4 Projection{ glm::mat4x4(1.0f) };
    glm::aligned_mat4x4 LightSpaceMatrix{ glm::mat4x4(1.0f) };
} ;

struct FMaterial
{
    alignas(16) float Shininess;
};

struct FLight
{
    glm::aligned_vec3 Position;
    glm::aligned_vec3 Ambient;
    glm::aligned_vec3 Diffuse;
    glm::aligned_vec3 Specular;
};

struct FLightMaterial
{
    FMaterial         Material;
    FLight            Light;
    glm::aligned_vec3 ViewPos;
} ;

// Keep CPU uniform layouts identical to the reflected SPIR-V block layouts.
static_assert(sizeof(int) == 4 && sizeof(float) == 4);
static_assert(sizeof(glm::mat4x4) == 64 && sizeof(glm::vec4) == 16);
static_assert(std::is_standard_layout_v<FGameArgs>);
static_assert(std::is_trivially_copyable_v<FGameArgs>);
static_assert(offsetof(FGameArgs, Resolution) == 0);
static_assert(offsetof(FGameArgs, FovRadians) == 8);
static_assert(offsetof(FGameArgs, Time) == 12);
static_assert(offsetof(FGameArgs, GameTime) == 16);
static_assert(offsetof(FGameArgs, TimeDelta) == 20);
static_assert(offsetof(FGameArgs, TimeRate) == 24);
static_assert(sizeof(FGameArgs) == 32);
static_assert(std::is_standard_layout_v<FBlackHoleArgs>);
static_assert(std::is_trivially_copyable_v<FBlackHoleArgs>);
static_assert(offsetof(FBlackHoleArgs, InverseCamRot) == 0);
static_assert(offsetof(FBlackHoleArgs, BlackHoleRelativePosRs) == 64);
static_assert(offsetof(FBlackHoleArgs, BlackHoleRelativeDiskNormal) == 80);
static_assert(offsetof(FBlackHoleArgs, BlackHoleRelativeDiskTangen) == 96);
static_assert(offsetof(FBlackHoleArgs, CameraVelocity) == 112);
static_assert(offsetof(FBlackHoleArgs, ie1_up) == 128);
static_assert(offsetof(FBlackHoleArgs, ie2_up) == 144);
static_assert(offsetof(FBlackHoleArgs, ie3_up) == 160);
static_assert(offsetof(FBlackHoleArgs, iU_up) == 176);
static_assert(offsetof(FBlackHoleArgs, iCamDataCoordisOutgoing) == 192);
static_assert(offsetof(FBlackHoleArgs, DEBUG) == 196);
static_assert(offsetof(FBlackHoleArgs, Prepass) == 200);
static_assert(offsetof(FBlackHoleArgs, Whitehole) == 204);
static_assert(offsetof(FBlackHoleArgs, InWhichUniverse) == 208);
static_assert(offsetof(FBlackHoleArgs, Grid) == 212);
static_assert(offsetof(FBlackHoleArgs, EnableHeatHaze) == 216);
static_assert(offsetof(FBlackHoleArgs, EnableShadowCulling) == 220);
static_assert(offsetof(FBlackHoleArgs, ObserverMode) == 224);
static_assert(offsetof(FBlackHoleArgs, Polarization) == 228);
static_assert(offsetof(FBlackHoleArgs, UseImageDisk) == 232);
static_assert(offsetof(FBlackHoleArgs, Quality) == 236);
static_assert(offsetof(FBlackHoleArgs, UniverseSign) == 240);
static_assert(offsetof(FBlackHoleArgs, BlackHoleTime) == 244);
static_assert(offsetof(FBlackHoleArgs, BlackHoleMassSol) == 248);
static_assert(offsetof(FBlackHoleArgs, Spin) == 252);
static_assert(offsetof(FBlackHoleArgs, Q2) == 256);
static_assert(offsetof(FBlackHoleArgs, Mu) == 260);
static_assert(offsetof(FBlackHoleArgs, AccretionRate) == 264);
static_assert(offsetof(FBlackHoleArgs, BackShiftMax) == 268);
static_assert(offsetof(FBlackHoleArgs, DensestarsurfaceR) == 272);
static_assert(offsetof(FBlackHoleArgs, DensestarBlackbodyIntensityExponent) == 276);
static_assert(offsetof(FBlackHoleArgs, DensestarRedShiftColorExponent) == 280);
static_assert(offsetof(FBlackHoleArgs, DensestarRedShiftIntensityExponent) == 284);
static_assert(offsetof(FBlackHoleArgs, DensestarBrightmut) == 288);
static_assert(offsetof(FBlackHoleArgs, InterRadiusRs) == 292);
static_assert(offsetof(FBlackHoleArgs, OuterRadiusRs) == 296);
static_assert(offsetof(FBlackHoleArgs, ThinRs) == 300);
static_assert(offsetof(FBlackHoleArgs, Hopper) == 304);
static_assert(offsetof(FBlackHoleArgs, Brightmut) == 308);
static_assert(offsetof(FBlackHoleArgs, Darkmut) == 312);
static_assert(offsetof(FBlackHoleArgs, Reddening) == 316);
static_assert(offsetof(FBlackHoleArgs, Saturation) == 320);
static_assert(offsetof(FBlackHoleArgs, BlackbodyIntensityExponent) == 324);
static_assert(offsetof(FBlackHoleArgs, RedShiftColorExponent) == 328);
static_assert(offsetof(FBlackHoleArgs, RedShiftIntensityExponent) == 332);
static_assert(offsetof(FBlackHoleArgs, ImageRotationSpeed) == 336);
static_assert(offsetof(FBlackHoleArgs, PolarizationAngle) == 340);
static_assert(offsetof(FBlackHoleArgs, HeatHaze) == 344);
static_assert(offsetof(FBlackHoleArgs, BackgroundBrightmut) == 348);
static_assert(offsetof(FBlackHoleArgs, PhotonRingBoost) == 352);
static_assert(offsetof(FBlackHoleArgs, PhotonRingColorTempBoost) == 356);
static_assert(offsetof(FBlackHoleArgs, BoostRot) == 360);
static_assert(offsetof(FBlackHoleArgs, JetRedShiftIntensityExponent) == 364);
static_assert(offsetof(FBlackHoleArgs, JetBrightmut) == 368);
static_assert(offsetof(FBlackHoleArgs, JetSaturation) == 372);
static_assert(offsetof(FBlackHoleArgs, JetShiftMax) == 376);
static_assert(offsetof(FBlackHoleArgs, BlendWeight) == 380);
static_assert(sizeof(FBlackHoleArgs) == 384);
static_assert(offsetof(FMatrices, View) == 0);
static_assert(offsetof(FMatrices, Projection) == 64);
static_assert(offsetof(FMatrices, LightSpaceMatrix) == 128);
static_assert(offsetof(FLightMaterial, Material) == 0);
static_assert(offsetof(FLightMaterial, Light) == 16);
static_assert(offsetof(FLightMaterial, ViewPos) == 80);
static_assert(sizeof(FMatrices) == 192 && sizeof(FLightMaterial) == 96);

extern FGameArgs GameArgs;
extern FBlackHoleArgs BlackHoleArgs;
extern FMatrices Matrices;
extern FLightMaterial LightMaterial;
extern float cfov;
extern float camsmth;
extern float qfm;
extern double g_Q;
extern double g_P;
