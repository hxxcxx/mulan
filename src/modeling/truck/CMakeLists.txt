# modeling_truck - optional truck-bridge backend.
#
# This backend is opt-in at both build time and runtime:
#   - CMake: MULAN_ENABLE_TRUCK_BACKEND=ON
#   - Runtime: MULAN_MODELING_BACKEND=truck
#
# Without the runtime environment variable it does not register ShapeOps, so
# the default OCCT backend remains untouched even if the DLL is present.
add_library(modeling_truck SHARED
    truck_shape_storage.cpp
    truck_shape_ops.cpp
    backend_entry.cpp
)

target_link_libraries(modeling_truck
    PRIVATE
        modeling_core
        truck_bridge
)

set_target_properties(modeling_truck PROPERTIES OUTPUT_NAME mulan_modeling_truck)

target_compile_definitions(modeling_truck PRIVATE
    BUILDING_MODELING_TRUCK
    $<$<CONFIG:Debug>:_DEBUG>
)

set(TRUCK_BACKEND_DEST "$<TARGET_FILE_DIR:mulan>/backends")
add_custom_command(TARGET modeling_truck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TRUCK_BACKEND_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:modeling_truck>" "${TRUCK_BACKEND_DEST}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:truck_bridge>" "$<TARGET_FILE_DIR:mulan>/"
    COMMENT "Deploying truck backend DLL and truck_bridge runtime..."
)
