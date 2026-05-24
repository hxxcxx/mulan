/**
 * @file PolylineAssembly.h
 * @brief 从碰撞线段组装有序折线
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "Collision.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace MulanGeo::BRep::boolean {

using geometry::Point3;
using geometry::near;

inline std::vector<tessellation::PolylineCurve> assemblePolylines(
    const std::vector<tessellation::LineSegment>& segments,
    double tol = geometry::TOLERANCE * 10.0)
{
    if (segments.empty()) return {};

    std::vector<bool> used(segments.size(), false);
    std::vector<tessellation::PolylineCurve> polylines;

    for (size_t start = 0; start < segments.size(); ++start) {
        if (used[start]) continue;
        used[start] = true;

        tessellation::PolylineCurve plc;
        plc.points.push_back(segments[start].p[0]);
        plc.points.push_back(segments[start].p[1]);
        plc.params.push_back(0.0);
        plc.params.push_back(1.0);

        bool extended = true;
        while (extended) {
            extended = false;
            Point3 head = plc.points.front();
            Point3 tail = plc.points.back();

            for (size_t j = 0; j < segments.size(); ++j) {
                if (used[j]) continue;

                if (near(tail, segments[j].p[0], tol)) {
                    plc.points.push_back(segments[j].p[1]);
                    plc.params.push_back(static_cast<double>(plc.params.size()));
                    tail = segments[j].p[1];
                    used[j] = true;
                    extended = true;
                } else if (near(tail, segments[j].p[1], tol)) {
                    plc.points.push_back(segments[j].p[0]);
                    plc.params.push_back(static_cast<double>(plc.params.size()));
                    tail = segments[j].p[0];
                    used[j] = true;
                    extended = true;
                } else if (near(head, segments[j].p[0], tol)) {
                    plc.points.insert(plc.points.begin(), segments[j].p[1]);
                    plc.params.insert(plc.params.begin(), 0.0);
                    head = segments[j].p[1];
                    used[j] = true;
                    extended = true;
                } else if (near(head, segments[j].p[1], tol)) {
                    plc.points.insert(plc.points.begin(), segments[j].p[0]);
                    plc.params.insert(plc.params.begin(), 0.0);
                    head = segments[j].p[0];
                    used[j] = true;
                    extended = true;
                }
            }
        }

        polylines.push_back(std::move(plc));
    }

    return polylines;
}

} // namespace MulanGeo::BRep::boolean
