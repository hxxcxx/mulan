# document - 与导入器和编辑器无关的文档数据模型。
add_library(document STATIC
    document.cpp
)

mulan_copy_public_headers(document document
    document.h
)

target_link_libraries(document
    PUBLIC
        asset
        modeling_core
        scene
)

set_target_properties(document PROPERTIES OUTPUT_NAME mulan_document)
