/*
 * IBL 全屏三角形 VS — 共用顶点着色器
 *
 * 无顶点缓冲：用 SV_VertexID 生成覆盖 NDC 的单个大三角形。
 * 输出 uv ∈ [0,1] 给 fragment（按屏幕坐标）。
 */

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID) {
    // id = 0,1,2 → 三个顶点构成覆盖 [-1,+1] 的大三角形
    // 经典 trick：避免 2-triangle quad 的接缝开销
    float2 p = float2((id << 1) & 2, id & 2);   // (0,0),(2,0),(0,2)
    VSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);     // (-1,-1),(3,-1),(-1,3)
    o.uv  = p;                                    // [0,0],[1,0],[0,1]
    return o;
}
