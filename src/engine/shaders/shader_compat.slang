/*
 * Cross-target resource helpers.
 *
 * DX11, DX12, and Vulkan use separate textures and sampler states. Desktop
 * OpenGL consumes combined sampler uniforms, so the OpenGL target declares
 * Sampler2D resources and omits the explicit sampler argument at call sites.
 */

#ifndef MULAN_SHADER_COMPAT_SLANG
#define MULAN_SHADER_COMPAT_SLANG

#ifdef MULAN_OPENGL
#define MULAN_SAMPLE_2D(texture, sampler, coordinates) texture.Sample(coordinates)
#else
#define MULAN_SAMPLE_2D(texture, sampler, coordinates) texture.Sample(sampler, coordinates)
#endif

#endif // MULAN_SHADER_COMPAT_SLANG
