#include "engine_settings_dialog.h"
#include "engine_settings.h"

#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>

using namespace mulan::engine;

EngineSettingsDialog::EngineSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Engine Settings"));
    setMinimumWidth(320);

    auto* layout = new QFormLayout(this);

    // --- 渲染后端 ---
    combo_backend_ = new QComboBox(this);
    combo_backend_->addItem("Vulkan",   static_cast<int>(GraphicsBackend::Vulkan));
    combo_backend_->addItem("D3D12",    static_cast<int>(GraphicsBackend::D3D12));
    layout->addRow(tr("Render Backend:"), combo_backend_);

    // --- 抗锯齿 ---
    combo_msaa_ = new QComboBox(this);
    combo_msaa_->addItem("None",  static_cast<int>(RenderConfig::MSAALevel::None));
    combo_msaa_->addItem("MSAA 2x", static_cast<int>(RenderConfig::MSAALevel::x2));
    combo_msaa_->addItem("MSAA 4x", static_cast<int>(RenderConfig::MSAALevel::x4));
    combo_msaa_->addItem("MSAA 8x", static_cast<int>(RenderConfig::MSAALevel::x8));
    layout->addRow(tr("Anti-Aliasing:"), combo_msaa_);

    // --- 背景色 ---
    button_color_ = new QPushButton(this);
    connect(button_color_, &QPushButton::clicked, this, &EngineSettingsDialog::onChooseBackgroundColor);
    layout->addRow(tr("Background:"), button_color_);

    // --- IBL 开关 ---
    check_ibl_ = new QCheckBox(tr("Enable IBL (environment reflections)"), this);
    layout->addRow(check_ibl_);

    // --- HDR 路径 ---
    edit_hdr_path_ = new QLineEdit(this);
    edit_hdr_path_->setPlaceholderText(tr("assets/envmap.hdr"));
    layout->addRow(tr("HDR Path:"), edit_hdr_path_);

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
    int idx = combo_backend_->findData(static_cast<int>(s.backend()));
    if (idx >= 0) combo_backend_->setCurrentIndex(idx);
    idx = combo_msaa_->findData(static_cast<int>(s.msaa()));
    if (idx >= 0) combo_msaa_->setCurrentIndex(idx);
    background_color_ = s.backgroundColor();
    updateBackgroundButton();
    check_ibl_->setChecked(s.iblEnabled());
    edit_hdr_path_->setText(s.hdrPath());
}

void EngineSettingsDialog::onAccept() {
    auto& s = EngineSettings::instance();
    s.setBackend(static_cast<GraphicsBackend>(combo_backend_->currentData().toInt()));
    s.setMsaa(static_cast<RenderConfig::MSAALevel>(combo_msaa_->currentData().toInt()));
    s.setBackgroundColor(background_color_);
    s.setIblEnabled(check_ibl_->isChecked());
    s.setHdrPath(edit_hdr_path_->text().trimmed());
    accept();
}

void EngineSettingsDialog::onReject() {
    reject();
}

void EngineSettingsDialog::onChooseBackgroundColor() {
    QColor color = QColorDialog::getColor(background_color_, this, tr("Background Color"), QColorDialog::ShowAlphaChannel);
    if (!color.isValid()) return;
    background_color_ = color;
    updateBackgroundButton();
}

void EngineSettingsDialog::updateBackgroundButton() {
    if (!button_color_) return;
    button_color_->setText(background_color_.name(QColor::HexArgb).toUpper());
    button_color_->setStyleSheet(QString("background-color: %1;").arg(background_color_.name(QColor::HexArgb)));
}
