add_library(graphics INTERFACE)

mulan_copy_public_headers(graphics graphics)

target_link_libraries(graphics INTERFACE math)
