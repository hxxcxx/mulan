/**
 * @file recent_thumbnail_service.h
 * @brief 从文档视口生成并原子保存最近文件缩略图。
 * @author hxxcxx
 * @date 2026-07-19
 */
#pragma once

#include <QString>

class DocumentViewport;

class RecentThumbnailService {
public:
    /// 成功返回缩略图绝对路径；缩略图是非关键功能，失败返回空字符串。
    QString capture(DocumentViewport& viewport, const QString& documentPath) const;
};
