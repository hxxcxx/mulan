/**
 * @file DocumentIO.h
 * @brief Document 的序列化保存/加载 — 完整闭环
 * @author hxxcxx
 * @date 2026-05-19
 *
 * 完整流程：
 *   保存: Document → Entity[] → Geometry(多态) → BinaryArchive → .mulan 文件
 *   加载: .mulan 文件 → BinaryArchive → Geometry(多态恢复) → Entity[] → Document
 *
 * 多态序列化核心：
 *   保存时写 Geometry 的类名（如 "BoxGeometry"）+ 参数
 *   加载时读类名 → ObjectFactory::create() → 反序列化参数
 *   参数化几何体只存几个 double，文件极小
 */
#pragma once

#include "DocumentExport.h"
#include "Document.h"
#include "Entity.h"
#include "EntityId.h"
#include "Geometry.h"

#include "mulan/core/serialization/BinaryArchive.h"
#include "mulan/core/reflection/Object.h"

#include <filesystem>
#include <string>

namespace mulan::Document {

// ============================================================
// 文件格式版本
// ============================================================

static constexpr uint32_t kDocumentFormatVersion = 1;
static constexpr uint32_t kEntityVersion         = 1;
static constexpr uint32_t kGeometryVersion       = 1;

// ============================================================
// 保存 Document 到文件
// ============================================================

inline bool saveDocument(const Document& doc, const std::string& filePath) {
    core::BinaryOutputArchive ar(filePath);
    if (!ar.isOpen()) return false;

    // 文件头：魔数 + 版本
    ar.setVersion(kDocumentFormatVersion);
    ar << std::string("MulanGeoDoc");  // 魔数标识

    // Document 元信息
    ar << doc.displayName();
    ar << doc.entityCount();

    // 遍历所有 Entity 并序列化
    doc.forEachEntity([&](const Entity& entity) {
        // --- Entity 基本信息 ---
        ar << entity.id().value;
        ar << entity.name();
        ar << entity.visible();

        // 变换矩阵（16 个 double）
        const auto& t = entity.localTransform();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                ar << t[i][j];

        // 父实体 ID
        ar << entity.parentId().value;

        // --- 元数据 ---
        auto& meta = entity.allMetadata();
        ar << static_cast<uint64_t>(meta.size());
        for (auto& [key, val] : meta) {
            ar << key;
            // 简化：只存 string 类型元数据
            if (std::holds_alternative<std::string>(val)) {
                ar << static_cast<uint8_t>(0);
                ar << std::get<std::string>(val);
            } else if (std::holds_alternative<double>(val)) {
                ar << static_cast<uint8_t>(1);
                ar << std::get<double>(val);
            } else if (std::holds_alternative<int64_t>(val)) {
                ar << static_cast<uint8_t>(2);
                ar << std::get<int64_t>(val);
            } else if (std::holds_alternative<bool>(val)) {
                ar << static_cast<uint8_t>(3);
                ar << std::get<bool>(val);
            }
        }

        // --- Geometry（多态序列化核心）---
        bool hasGeom = entity.hasGeometry();
        ar << hasGeom;
        if (hasGeom) {
            const Geometry* geom = entity.geometry();
            // 写入类名（用于工厂创建）
            ar << geom->classInfo().name();
            // 写入 GeometryType 枚举
            ar << static_cast<uint8_t>(geom->geometryType());
            // 多态序列化：调用具体 Geometry 的 serialize
            geom->serialize(ar);
        }
    });

    return true;
}

// ============================================================
// 从文件加载 Document
// ============================================================

inline std::unique_ptr<Document> loadDocument(const std::string& filePath) {
    core::BinaryInputArchive ar(filePath);
    if (!ar.isOpen()) return nullptr;

    // 读文件头
    std::string magic;
    ar >> magic;
    if (magic != "MulanGeoDoc") return nullptr;

    auto doc = Document::create();

    // 读 Document 元信息
    std::string displayName;
    ar >> displayName;
    doc->setDisplayName(std::move(displayName));

    uint64_t entityCount = 0;
    ar >> entityCount;

    for (uint64_t i = 0; i < entityCount; ++i) {
        // --- Entity 基本信息 ---
        uint64_t idVal;
        ar >> idVal;
        std::string name;
        ar >> name;
        bool visible;
        ar >> visible;

        // 变换矩阵
        engine::Mat4 transform(1.0);
        for (int ri = 0; ri < 4; ++ri)
            for (int ci = 0; ci < 4; ++ci)
                ar >> transform[ri][ci];

        // 父实体 ID
        uint64_t parentIdVal;
        ar >> parentIdVal;

        // 元数据
        uint64_t metaCount = 0;
        ar >> metaCount;
        std::unordered_map<std::string, Entity::Metadata> metadata;
        for (uint64_t m = 0; m < metaCount; ++m) {
            std::string key;
            ar >> key;
            uint8_t typeTag;
            ar >> typeTag;
            switch (typeTag) {
            case 0: { std::string v; ar >> v; metadata[key] = v; break; }
            case 1: { double v;      ar >> v; metadata[key] = v; break; }
            case 2: { int64_t v;     ar >> v; metadata[key] = v; break; }
            case 3: { bool v;        ar >> v; metadata[key] = v; break; }
            }
        }

        // --- Geometry（多态反序列化核心）---
        std::unique_ptr<Geometry> geom;
        bool hasGeom;
        ar >> hasGeom;
        if (hasGeom) {
            std::string className;
            ar >> className;
            uint8_t geomType;
            ar >> geomType;

            // 通过 ObjectFactory 创建对应类型
            auto obj = core::ObjectFactory::instance().create(className);
            if (obj) {
                geom = std::unique_ptr<Geometry>(
                    static_cast<Geometry*>(obj.release()));
                geom->serialize(ar);  // 多态反序列化
            }
        }

        // 创建 Entity（使用指定 ID 恢复）
        auto entityId = EntityId::fromValue(idVal);
        auto entity = std::make_unique<Entity>(entityId, std::move(name));
        entity->setVisible(visible);
        entity->setLocalTransform(transform);
        if (parentIdVal != 0)
            entity->setParentId(EntityId::fromValue(parentIdVal));
        for (auto& [k, v] : metadata)
            entity->setMetadata(k, v);
        if (geom)
            entity->setGeometry(std::move(geom));

        // TODO: 将 entity 添加到 document（需要 Document 提供 addEntityWithId 接口）
    }

    return doc;
}

} // namespace mulan::Document
