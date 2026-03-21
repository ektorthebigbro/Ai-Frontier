#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float highlightAmount;
    vec2 resolution;
    vec2 scenePos;
    vec2 sceneSize;
    vec2 viewportSize;
    vec2 backdropTextureSize;
    vec2 cornerData;
} ubuf;

layout(binding = 1) uniform sampler2D backdrop;

const float PI = 3.14159265359;
const float IOR = 1.5;
const float F0  = 0.04;
const float ROUGHNESS = 0.32;

float roundedRectSDF(vec2 p, vec2 halfSize, float radius)
{
    vec2 q = abs(p) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

float fresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distributionGGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySmith(float NdotV, float NdotL, float alpha)
{
    float r = alpha + 1.0;
    float k = (r * r) / 8.0;
    float g1 = NdotV / (NdotV * (1.0 - k) + k);
    float g2 = NdotL / (NdotL * (1.0 - k) + k);
    return g1 * g2;
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 viewport = max(ubuf.viewportSize, vec2(1.0, 1.0));

    vec2 halfSize = ubuf.sceneSize * 0.5;
    vec2 localPx  = uv * ubuf.sceneSize - halfSize;
    float cRadius = clamp(ubuf.cornerData.x, 0.0,
                          min(ubuf.sceneSize.x, ubuf.sceneSize.y) * 0.5 - 0.5);
    float sdf       = roundedRectSDF(localPx, halfSize, cRadius);
    float shapeMask = 1.0 - smoothstep(-0.5, 0.5, sdf);
    if (shapeMask <= 0.0) { fragColor = vec4(0.0); return; }

    vec3 N = vec3(0.0, 0.0, 1.0);
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(-0.35, -0.25, 0.90));
    vec3 H = normalize(V + L);
    float NdotV = 1.0;
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    float f0Eff = F0 + ubuf.highlightAmount * 0.02;
    float F = fresnelSchlick(NdotV, f0Eff);

    vec2 sceneUv = (ubuf.scenePos + uv * ubuf.sceneSize) / viewport;

    vec3 backdrop_rgb = texture(backdrop, sceneUv).rgb;

    vec3 absorp = exp(-vec3(0.025, 0.022, 0.018) * 1.0);
    backdrop_rgb *= absorp;

    vec3 transmitted = (1.0 - F) * backdrop_rgb;

    float a2   = ROUGHNESS * ROUGHNESS;
    float D    = distributionGGX(NdotH, a2);
    float G    = geometrySmith(NdotV, NdotL, a2);
    float spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 specular = vec3(1.0) * spec * NdotL;

    float lum     = dot(backdrop_rgb, vec3(0.2126, 0.7152, 0.0722));
    float scatter = ROUGHNESS * 0.06;
    vec3 scattered = vec3(lum * 0.4 + 0.015) * scatter;

    float borderInner = smoothstep(0.0, 1.0, -sdf);
    float borderOuter = smoothstep(1.5, 0.5, -sdf);
    float borderMask  = borderInner * borderOuter;
    vec3 borderColor  = vec3(1.0) * borderMask * 0.18;

    vec3 color     = transmitted + scattered + specular + borderColor;
    float outAlpha = 0.94 * shapeMask;

    fragColor = vec4(color * outAlpha, outAlpha) * ubuf.qt_Opacity;
}
