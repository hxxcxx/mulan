/**
 * @file bspline_basis.cpp
 * @brief B-spline 基函数 — Cox-de Boor 递归 + 节点区间查找 + 节点向量生成 — 实现
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "bspline_basis.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// 节点区间查找 —— NURBS Book A2.1
// ============================================================

int bsplineFindSpan(int n, int p, double u, const std::vector<double>& U) noexcept {
    // 右端点闭合：u 在最后节点上时，归到最后一个 span
    if (u >= U[n + 1])
        return n;
    if (u <= U[p])
        return p;

    // 线性查找（学习实现；大规模可换二分）
    int k = p;
    for (int i = p; i <= n; ++i) {
        if (u < U[i + 1]) {
            k = i;
            break;
        }
        k = i;
    }
    return k;
}

// ============================================================
// Cox-de Boor 基函数求值 —— NURBS Book A2.2（单点单基函数）
// ============================================================

double bsplineBasis(int i, int p, double u, const std::vector<double>& U) noexcept {
    if (i < 0 || i + p + 1 >= static_cast<int>(U.size()))
        return 0.0;

    // 动态规划表：N[j] 表示当前层 N_{i+j, layer}
    // 从 p=0 层起步，逐层向上累加
    // 这里用递归实现，清晰对应数学定义；调用次数少时性能足够
    if (p == 0) {
        // N_{i,0} = 1 if U[i] <= u < U[i+1]（半开区间）
        // 右端点闭合：u == U.back() 时，仅最后一个非零长度区间 [U[i], U[i+1]] 命中
        const int last = static_cast<int>(U.size()) - 1;
        if (u >= U[i] && u < U[i + 1])
            return 1.0;
        if (i + 1 == last && u == U[last] && U[i] < U[i + 1])
            return 1.0;
        return 0.0;
    }

    const double eps = Tolerance::defaultValue().paramEps;
    double result = 0.0;

    // 第一项：(u - U[i]) / (U[i+p] - U[i]) * N_{i,p-1}(u)
    double d1 = U[i + p] - U[i];
    if (std::abs(d1) > eps) {
        result += (u - U[i]) / d1 * bsplineBasis(i, p - 1, u, U);
    }
    // 第二项：(U[i+p+1] - u) / (U[i+p+1] - U[i+1]) * N_{i+1,p-1}(u)
    double d2 = U[i + p + 1] - U[i + 1];
    if (std::abs(d2) > eps) {
        result += (U[i + p + 1] - u) / d2 * bsplineBasis(i + 1, p - 1, u, U);
    }
    return result;
}

std::vector<double> bsplineBasisFunctions(int p, int k, double u, const std::vector<double>& U) {
    std::vector<double> N(p + 1, 0.0);
    std::vector<double> left(p + 1, 0.0);
    std::vector<double> right(p + 1, 0.0);

    N[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[j] = u - U[k + 1 - j];
        right[j] = U[k + j] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            double temp = N[r] / (right[r + 1] + left[j - r]);
            N[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        N[j] = saved;
    }
    return N;
}

// ============================================================
// 节点向量生成
// ============================================================

std::vector<double> clampedKnotVector(int degree, int controlPointCount) {
    // n = controlPointCount - 1；总节点数 = n + p + 2 = controlPointCount + degree + 1
    // 内部节点数 = n - p = controlPointCount - 1 - degree
    const int numInterior = controlPointCount - 1 - degree;
    std::vector<double> U;
    U.reserve(controlPointCount + degree + 1);

    for (int i = 0; i <= degree; ++i)
        U.push_back(0.0);
    for (int i = 1; i <= numInterior; ++i) {
        U.push_back(static_cast<double>(i) / (numInterior + 1));
    }
    for (int i = 0; i <= degree; ++i)
        U.push_back(1.0);
    return U;
}

std::vector<double> uniformKnotVector(int degree, int controlPointCount) {
    const int total = controlPointCount + degree + 1;  // 节点数 = m + 1，m = total - 1
    std::vector<double> U(total);
    const double step = 1.0 / (total - 1);
    for (int i = 0; i < total; ++i)
        U[i] = i * step;
    return U;
}

}  // namespace mulan::math
