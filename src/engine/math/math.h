/**
 * @file math.h
 * @brief GLM 集成 — 统一数学类型别名与常用函数
 *
 * 场景/应用层使用 double 精度 (dvec3/dmat4/dquat)
 * 渲染层上传 GPU 时转换为 float (glm::mat4)
 *
 * @author hxxcxx
 * @date 2026-04-20
 */

#pragma once

// GLM 配置
#define GLM_FORCE_CTOR_INIT           // 确保默认构造零初始化
#define GLM_ENABLE_EXPERIMENTAL       // 启用 GTX 扩展

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>       // value_ptr
#include <glm/gtx/norm.hpp>           // length2, distance2
#include <glm/gtx/quaternion.hpp>     // angleAxis, quat casting

namespace mulan::engine {

// ============================================================
// 场景层类型别名 — double 精度
// ============================================================
using Vec3 = glm::dvec3;
using Vec4 = glm::dvec4;
using Mat3 = glm::dmat3;
using Mat4 = glm::dmat4;
using Quat = glm::dquat;

// ============================================================
// 渲染层类型别名 — float 精度（GPU 友好）
// ============================================================
using FVec3 = glm::vec3;
using FVec4 = glm::vec4;
using FMat4 = glm::mat4;
using FQuat = glm::quat;

} // namespace mulan::engine
