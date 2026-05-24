/**
 * @file EngineSettingsDialog.cpp
 * @brief 引擎设置对话框实现
 * @author hxxcxx
 * @date 2026-04-24
 */
#include "EngineSettingsDialog.h"
#include "EngineSettings.h"

#include <QFormLayout>
#include <QComboBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>

using namespace MulanGeo::engine;

EngineSettingsDialog::EngineSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Engine Settings"));
    setMinimumWidth(320);

    auto* layout = new QFormLayout(this);

    // --- 渲染后端 ---
    m_comboBackend = new QComboBox(this);
    m_comboBackend->addItem("OpenGL",   static_cast<int>(GraphicsBackend::OpenGL));
    m_comboBackend->addItem("Vulkan",   static_cast<int>(GraphicsBackend::Vulkan));
    m_comboBackend->addItem("D3D12",    static_cast<int>(GraphicsBackend::D3D12));
    m_comboBackend->addItem("D3D11",    static_cast<int>(GraphicsBackend::D3D11));
    layout->addRow(tr("Render Backend:"), m_comboBackend);

    // --- 抗锯齿 ---
    m_comboMsaa = new QComboBox(this);
    m_comboMsaa->addItem("None",  static_cast<int>(RenderConfig::MSAALevel::None));
    m_comboMsaa->addItem("MSAA 2x", static_cast<int>(RenderConfig::MSAALevel::x2));
    m_comboMsaa->addItem("MSAA 4x", static_cast<int>(RenderConfig::MSAALevel::x4));
    m_comboMsaa->addItem("MSAA 8x", static_cast<int>(RenderConfig::MSAALevel::x8));
    layout->addRow(tr("Anti-Aliasing:"), m_comboMsaa);

    // --- 背景色 ---
    m_buttonColor = new QPushButton(this);
    connect(m_buttonColor, &QPushButton::clicked, this, &EngineSettingsDialog::onChooseBackgroundColor);
    layout->addRow(tr("Background:"), m_buttonColor);

    // --- 提示 ---
    auto* hint = new QLabel(tr("Changes take effect on next document tab."), this);
    hint->setStyleSheet("color: gray; font-size: 10pt;");
    layout->addRow(hint);

    // --- 按钮 ---
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &EngineSettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EngineSettingsDialog::onReject);

    readSettings();
    adjustSize();
}

void EngineSettingsDialog::readSettings() {
    auto& s = EngineSettings::instance();
    int idx = m_comboBackend->findData(static_cast<int>(s.backend()));
    if (idx >= 0) m_comboBackend->setCurrentIndex(idx);
    idx = m_comboMsaa->findData(static_cast<int>(s.msaa()));
    if (idx >= 0) m_comboMsaa->setCurrentIndex(idx);
    m_backgroundColor = s.backgroundColor();
    updateBackgroundButton();
}

void EngineSettingsDialog::onAccept() {
    auto& s = EngineSettings::instance();
    s.setBackend(static_cast<GraphicsBackend>(m_comboBackend->currentData().toInt()));
    s.setMsaa(static_cast<RenderConfig::MSAALevel>(m_comboMsaa->currentData().toInt()));
    s.setBackgroundColor(m_backgroundColor);
    accept();
}

void EngineSettingsDialog::onReject() {
    reject();
}

void EngineSettingsDialog::onChooseBackgroundColor() {
    QColor color = QColorDialog::getColor(m_backgroundColor, this, tr("Background Color"), QColorDialog::ShowAlphaChannel);
    if (!color.isValid()) return;
    m_backgroundColor = color;
    updateBackgroundButton();
}

void EngineSettingsDialog::updateBackgroundButton() {
    if (!m_buttonColor) return;
    m_buttonColor->setText(m_backgroundColor.name(QColor::HexArgb).toUpper());
    m_buttonColor->setStyleSheet(QString("background-color: %1;").arg(m_backgroundColor.name(QColor::HexArgb)));
}
