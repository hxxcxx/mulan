/*
 * 传统材质 Tangent 顶点布局变体。着色语义与 legacy.frag 一致，
 * 法线贴图使用导入阶段生成或读取的 MikkTSpace tangent frame。
 */

#define MULAN_USE_VERTEX_TANGENT 1
#include "legacy.frag.slang"
