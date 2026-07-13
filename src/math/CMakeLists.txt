# Math - static library for linear algebra, geometry, curves, surfaces, and spatial structures.
add_library(math STATIC
    algo/intersect.cpp
    algo2d/convex_hull.cpp
    algo2d/triangulation.cpp
    algo2d/voronoi.cpp
    algo2d/polygon_intersect.cpp
    basis/bspline_basis.cpp
    surface/bezier_surface.cpp
    surface/bspline_surface.cpp
    surface/nurbs_surface.cpp
    topology/dcel.cpp
    spatial/kdtree.cpp
    spatial/quadtree.cpp
    spatial/rtree.cpp
    spatial/bsptree.cpp
    spatial/bvhtree.cpp
)

mulan_copy_public_headers(math math)

# DCEL validation uses the shared logging implementation from core. Keep the
# dependency private because no public math header exposes core types.
target_link_libraries(math PRIVATE core)

# math is static today, so MATH_API remains empty. Define BUILDING_MATH if this target becomes shared.
