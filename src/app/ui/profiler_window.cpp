#include "profiler_window.h"

#include <mulan/core/profiling/profile.h>

#include <QApplication>
#include <QCoreApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kNanosecondsRole = Qt::UserRole + 1;
constexpr int kThreadTotalRole = Qt::UserRole + 2;
constexpr int kSortRole = Qt::UserRole + 3;

void configureProfilerScrollBar(QScrollBar* scrollBar) {
    scrollBar->setObjectName("profilerScrollBar");
    scrollBar->style()->unpolish(scrollBar);
    scrollBar->style()->polish(scrollBar);
    scrollBar->update();
}

QString formatDuration(std::uint64_t nanoseconds) {
    const double value = static_cast<double>(nanoseconds);
    if (nanoseconds >= 1'000'000'000ULL)
        return QStringLiteral("%1 s").arg(value / 1'000'000'000.0, 0, 'f', 2);
    if (nanoseconds >= 1'000'000ULL)
        return QStringLiteral("%1 ms").arg(value / 1'000'000.0, 0, 'f', 2);
    if (nanoseconds >= 1'000ULL)
        return QStringLiteral("%1 us").arg(value / 1'000.0, 0, 'f', 1);
    return QStringLiteral("%1 ns").arg(nanoseconds);
}

std::size_t countNodes(const std::vector<mulan::profiling::ProfileNode>& nodes) {
    std::size_t result = 0;
    for (const auto& node : nodes)
        result += 1 + countNodes(node.children);
    return result;
}

int countVisibleRows(const QTreeView* tree, const QModelIndex& parent = {}) {
    int result = 0;
    const QAbstractItemModel* model = tree->model();
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        ++result;
        if (tree->isExpanded(index))
            result += countVisibleRows(tree, index);
    }
    return result;
}

void resizeTreeToVisibleRows(QTreeView* tree) {
    constexpr int rowHeight = 27;
    tree->setFixedHeight(tree->header()->height() + countVisibleRows(tree) * rowHeight + tree->frameWidth() * 2);
}

std::uint64_t threadDuration(const mulan::profiling::ThreadProfile& thread) {
    std::uint64_t result = 0;
    for (const auto& root : thread.roots)
        result += root.inclusiveNanoseconds;
    return result;
}

class DurationBarDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize result = QStyledItemDelegate::sizeHint(option, index);
        result.setHeight(27);
        return result;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto nanoseconds = index.data(kNanosecondsRole).toULongLong();
        const auto total = index.data(kThreadTotalRole).toULongLong();
        if (nanoseconds != 0 && total != 0 && !(option.state & QStyle::State_Selected)) {
            const double ratio = std::clamp(static_cast<double>(nanoseconds) / static_cast<double>(total), 0.0, 1.0);
            QRect bar = option.rect.adjusted(3, 4, -3, -4);
            bar.setWidth(std::max(2, static_cast<int>(std::round(bar.width() * ratio))));
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(54, 181, 174, 42));
            painter->drawRoundedRect(bar, 3, 3);
            painter->restore();
        }
        QStyledItemDelegate::paint(painter, option, index);
    }
};

QList<QStandardItem*> makeNodeRow(const mulan::profiling::ProfileNode& node, std::uint64_t threadTotal) {
    auto* name = new QStandardItem(QString::fromStdString(node.name));
    auto* calls = new QStandardItem(QString::number(node.callCount));
    auto* inclusive = new QStandardItem(formatDuration(node.inclusiveNanoseconds));
    auto* self = new QStandardItem(formatDuration(node.selfNanoseconds));
    auto* average =
            new QStandardItem(formatDuration(node.callCount == 0 ? 0 : node.inclusiveNanoseconds / node.callCount));
    const double percentage = threadTotal == 0 ? 0.0
                                               : 100.0 * static_cast<double>(node.inclusiveNanoseconds) /
                                                         static_cast<double>(threadTotal);
    auto* percent = new QStandardItem(QStringLiteral("%1%").arg(percentage, 0, 'f', 1));

    name->setData(name->text(), kSortRole);
    calls->setData(QVariant::fromValue<qulonglong>(node.callCount), kSortRole);
    inclusive->setData(QVariant::fromValue<qulonglong>(node.inclusiveNanoseconds), kSortRole);
    self->setData(QVariant::fromValue<qulonglong>(node.selfNanoseconds), kSortRole);
    average->setData(
            QVariant::fromValue<qulonglong>(node.callCount == 0 ? 0 : node.inclusiveNanoseconds / node.callCount),
            kSortRole);
    percent->setData(QVariant::fromValue<qulonglong>(node.inclusiveNanoseconds), kSortRole);

    for (auto* item : std::array{ calls, inclusive, self, average, percent })
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    name->setData(QVariant::fromValue<qulonglong>(node.inclusiveNanoseconds), kNanosecondsRole);
    name->setData(QVariant::fromValue<qulonglong>(threadTotal), kThreadTotalRole);

    QList<QStandardItem*> row{ name, calls, inclusive, self, average, percent };
    for (const auto& child : node.children)
        name->appendRow(makeNodeRow(child, threadTotal));
    return row;
}

QFrame* makeMetricCard(const QString& label, const QString& value, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("profilerMetricCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(1);
    auto* valueLabel = new QLabel(value, card);
    valueLabel->setObjectName("profilerMetricValue");
    auto* labelWidget = new QLabel(label, card);
    labelWidget->setObjectName("profilerMetricLabel");
    layout->addWidget(valueLabel);
    layout->addWidget(labelWidget);
    return card;
}

}  // namespace

ProfilerWindow::ProfilerWindow(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Mulan Profiler"));
    setWindowModality(Qt::NonModal);
    setModal(false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(1060, 760);
    setMinimumSize(760, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Manual performance capture"), this);
    title->setObjectName("profilerTitle");
    layout->addWidget(title);

    auto* hint = new QLabel(
            tr("Record only the operation you want to inspect. Results appear here and are also saved as HTML."), this);
    hint->setObjectName("profilerHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* stateRow = new QHBoxLayout();
    status_label_ = new QLabel(this);
    status_label_->setObjectName("profilerStatus");
    elapsed_label_ = new QLabel(this);
    elapsed_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stateRow->addWidget(status_label_);
    stateRow->addStretch();
    stateRow->addWidget(elapsed_label_);
    layout->addLayout(stateRow);

    auto* captureRow = new QHBoxLayout();
    start_button_ = new QPushButton(tr("Start Recording"), this);
    start_button_->setObjectName("profilerStartButton");
    start_button_->setProperty("uiRole", "profilerPrimary");
    stop_button_ = new QPushButton(tr("Stop && Show Results"), this);
    stop_button_->setObjectName("profilerStopButton");
    stop_button_->setProperty("uiRole", "profilerDanger");
    captureRow->addWidget(start_button_);
    captureRow->addWidget(stop_button_);
    captureRow->addStretch();
    layout->addLayout(captureRow);

    auto* resultsTitleRow = new QHBoxLayout();
    auto* resultsTitle = new QLabel(tr("CAPTURE RESULT"), this);
    resultsTitle->setObjectName("profilerSectionTitle");
    auto* resultsHint = new QLabel(tr("One expandable call tree per thread"), this);
    resultsHint->setObjectName("profilerSectionHint");
    resultsTitleRow->addWidget(resultsTitle);
    resultsTitleRow->addStretch();
    resultsTitleRow->addWidget(resultsHint);
    layout->addLayout(resultsTitleRow);

    results_scroll_ = new QScrollArea(this);
    results_scroll_->setObjectName("profilerResultsScroll");
    results_scroll_->setWidgetResizable(true);
    results_scroll_->setFrameShape(QFrame::NoFrame);
    configureProfilerScrollBar(results_scroll_->verticalScrollBar());
    configureProfilerScrollBar(results_scroll_->horizontalScrollBar());
    results_content_ = new QWidget(results_scroll_);
    results_content_->setObjectName("profilerResultsContent");
    results_layout_ = new QVBoxLayout(results_content_);
    results_layout_->setContentsMargins(0, 0, 6, 0);
    results_layout_->setSpacing(12);
    empty_results_label_ =
            new QLabel(tr("NO CAPTURE DATA\nStart recording, exercise a feature, then stop to inspect its call trees."),
                       results_content_);
    empty_results_label_->setObjectName("profilerEmptyResults");
    empty_results_label_->setAlignment(Qt::AlignCenter);
    results_layout_->addWidget(empty_results_label_, 1);
    results_scroll_->setWidget(results_content_);
    layout->addWidget(results_scroll_, 1);

    auto* reportPanel = new QFrame(this);
    reportPanel->setObjectName("profilerReportPanel");
    auto* reportPanelLayout = new QHBoxLayout(reportPanel);
    reportPanelLayout->setContentsMargins(12, 9, 10, 9);
    reportPanelLayout->setSpacing(8);
    auto* reportTextLayout = new QVBoxLayout();
    reportTextLayout->setSpacing(1);
    auto* reportTitle = new QLabel(tr("HTML REPORT"), reportPanel);
    reportTitle->setObjectName("profilerReportTitle");
    report_label_ = new QLabel(tr("No report generated in this session."), reportPanel);
    report_label_->setObjectName("profilerReportPath");
    report_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reportTextLayout->addWidget(reportTitle);
    reportTextLayout->addWidget(report_label_);
    reportPanelLayout->addLayout(reportTextLayout, 1);
    open_button_ = new QPushButton(tr("Open Report"), reportPanel);
    open_button_->setProperty("uiRole", "profilerSecondary");
    auto* directoryButton = new QPushButton(tr("Reports Folder"), reportPanel);
    directoryButton->setProperty("uiRole", "profilerSecondary");
    auto* closeButton = new QPushButton(tr("Close"), reportPanel);
    closeButton->setProperty("uiRole", "profilerGhost");
    reportPanelLayout->addWidget(open_button_);
    reportPanelLayout->addWidget(directoryButton);
    reportPanelLayout->addWidget(closeButton);
    layout->addWidget(reportPanel);

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(100);
    connect(refresh_timer_, &QTimer::timeout, this, &ProfilerWindow::updateElapsedTime);
    connect(start_button_, &QPushButton::clicked, this, &ProfilerWindow::startCapture);
    connect(stop_button_, &QPushButton::clicked, this, &ProfilerWindow::stopCapture);
    connect(open_button_, &QPushButton::clicked, this, &ProfilerWindow::openLatestReport);
    connect(directoryButton, &QPushButton::clicked, this, &ProfilerWindow::openReportsDirectory);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    updateControls();
}

QString ProfilerWindow::reportsDirectory() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("profiles");
}

void ProfilerWindow::startCapture() {
    clearResults();
    mulan::profiling::startCapture();
    elapsed_.restart();
    refresh_timer_->start();
    updateControls();
}

void ProfilerWindow::stopCapture() {
    if (!mulan::profiling::isCapturing())
        return;
    const mulan::profiling::ProfileSnapshot result = mulan::profiling::stopCapture();
    refresh_timer_->stop();
    showResults(result);
    const std::string report = mulan::profiling::writeHtmlReportToDirectory(result, reportsDirectory().toStdString());
    if (report.empty()) {
        QMessageBox::warning(this, tr("Mulan Profiler"), tr("The HTML report could not be written."));
    } else {
        latest_report_ = QDir::fromNativeSeparators(QString::fromStdString(report));
        report_label_->setText(QDir::toNativeSeparators(latest_report_));
    }
    updateControls();
}

void ProfilerWindow::clearResults() {
    while (results_layout_->count() > 0) {
        QLayoutItem* item = results_layout_->takeAt(0);
        if (item->widget() != nullptr && item->widget() != empty_results_label_)
            delete item->widget();
        delete item;
    }
    empty_results_label_->show();
    results_layout_->addWidget(empty_results_label_, 1);
}

void ProfilerWindow::showResults(const mulan::profiling::ProfileSnapshot& snapshot) {
    clearResults();
    QLayoutItem* emptyItem = results_layout_->takeAt(0);
    delete emptyItem;
    empty_results_label_->hide();

    std::size_t nodeCount = 0;
    std::uint64_t totalNanoseconds = 0;
    for (const auto& thread : snapshot.threads) {
        nodeCount += countNodes(thread.roots);
        totalNanoseconds += threadDuration(thread);
    }

    auto* metrics = new QWidget(results_content_);
    metrics->setObjectName("profilerMetrics");
    auto* metricsLayout = new QHBoxLayout(metrics);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(8);
    metricsLayout->addWidget(makeMetricCard(tr("CAPTURED CPU TIME"), formatDuration(totalNanoseconds), metrics));
    metricsLayout->addWidget(makeMetricCard(tr("THREADS"), QString::number(snapshot.threads.size()), metrics));
    metricsLayout->addWidget(makeMetricCard(tr("ZONES"), QString::number(nodeCount), metrics));
    metricsLayout->addWidget(makeMetricCard(tr("FRAMES"), QString::number(snapshot.frameCount), metrics));
    results_layout_->addWidget(metrics);

    for (const auto& thread : snapshot.threads) {
        const std::uint64_t total = threadDuration(thread);
        auto* card = new QFrame(results_content_);
        card->setObjectName("profilerThreadCard");
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(1, 1, 1, 1);
        cardLayout->setSpacing(0);

        auto* header = new QWidget(card);
        header->setObjectName("profilerThreadHeader");
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(14, 9, 14, 9);
        auto* indicator = new QLabel(QStringLiteral("●"), header);
        indicator->setObjectName("profilerThreadIndicator");
        const QString threadName = thread.name.empty() ? tr("Unnamed thread") : QString::fromStdString(thread.name);
        auto* name = new QLabel(threadName, header);
        name->setObjectName("profilerThreadName");
        auto* id = new QLabel(tr("ID %1").arg(thread.id), header);
        id->setObjectName("profilerThreadMeta");
        auto* summary =
                new QLabel(tr("%1 zones   ·   %2").arg(countNodes(thread.roots)).arg(formatDuration(total)), header);
        summary->setObjectName("profilerThreadMeta");
        headerLayout->addWidget(indicator);
        headerLayout->addWidget(name);
        headerLayout->addWidget(id);
        headerLayout->addStretch();
        headerLayout->addWidget(summary);
        cardLayout->addWidget(header);

        auto* tree = new QTreeView(card);
        tree->setObjectName("profilerTree");
        auto* model = new QStandardItemModel(tree);
        model->setSortRole(kSortRole);
        model->setHorizontalHeaderLabels(
                { tr("ZONE"), tr("CALLS"), tr("INCLUSIVE"), tr("SELF"), tr("AVERAGE"), tr("THREAD %") });
        for (const auto& root : thread.roots)
            model->appendRow(makeNodeRow(root, total));
        tree->setModel(model);
        tree->setItemDelegate(new DurationBarDelegate(tree));
        tree->setAlternatingRowColors(false);
        tree->setUniformRowHeights(true);
        tree->setRootIsDecorated(true);
        tree->setIndentation(20);
        tree->setAnimated(false);
        tree->setSortingEnabled(false);
        tree->setExpandsOnDoubleClick(false);
        tree->setEditTriggers(QTreeView::NoEditTriggers);
        tree->setSelectionMode(QTreeView::NoSelection);
        tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        tree->expandToDepth(1);
        tree->header()->setFixedHeight(34);
        tree->header()->setSectionsClickable(false);
        tree->header()->setSortIndicatorShown(false);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column = 1; column < 6; ++column)
            tree->header()->setSectionResizeMode(column, QHeaderView::Fixed);
        tree->setColumnWidth(1, 76);
        tree->setColumnWidth(2, 116);
        tree->setColumnWidth(3, 104);
        tree->setColumnWidth(4, 116);
        tree->setColumnWidth(5, 104);
        connect(tree, &QTreeView::clicked, tree, [tree](const QModelIndex& index) {
            if (index.column() == 0 && tree->model()->hasChildren(index))
                tree->setExpanded(index, !tree->isExpanded(index));
        });
        connect(tree, &QTreeView::customContextMenuRequested, tree, [tree](const QPoint& position) {
            const QModelIndex index = tree->indexAt(position);
            if (!index.isValid() || index.column() != 0)
                return;
            QMenu menu(tree);
            QAction* copyAction = menu.addAction(QObject::tr("Copy Zone Name"));
            if (menu.exec(tree->viewport()->mapToGlobal(position)) == copyAction)
                QApplication::clipboard()->setText(index.data(Qt::DisplayRole).toString());
        });
        resizeTreeToVisibleRows(tree);
        connect(tree, &QTreeView::expanded, tree, [tree]() { resizeTreeToVisibleRows(tree); });
        connect(tree, &QTreeView::collapsed, tree, [tree]() { resizeTreeToVisibleRows(tree); });
        cardLayout->addWidget(tree);
        results_layout_->addWidget(card);
    }

    if (snapshot.threads.empty()) {
        empty_results_label_->setText(
                tr("NO ZONES CAPTURED\nThe selected interval completed without any instrumented calls."));
        empty_results_label_->show();
        results_layout_->addWidget(empty_results_label_, 1);
    } else {
        results_layout_->addStretch();
    }
}

void ProfilerWindow::openLatestReport() {
    if (!latest_report_.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(latest_report_));
}

void ProfilerWindow::openReportsDirectory() {
    QDir().mkpath(reportsDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(reportsDirectory()));
}

void ProfilerWindow::updateElapsedTime() {
    if (!elapsed_.isValid())
        return;
    const QString duration = tr("%1 s").arg(elapsed_.elapsed() / 1000.0, 0, 'f', 1);
    elapsed_label_->setText(mulan::profiling::isCapturing() ? duration : tr("Captured %1").arg(duration));
}

void ProfilerWindow::updateControls() {
    const bool capturing = mulan::profiling::isCapturing();
    const bool complete = !capturing && elapsed_.isValid();
    status_label_->setText(capturing ? tr("● Recording") : complete ? tr("● Capture complete") : tr("● Ready"));
    status_label_->setProperty("captureState", capturing ? "recording" : complete ? "complete" : "ready");
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
    start_button_->setEnabled(!capturing);
    stop_button_->setEnabled(capturing);
    open_button_->setEnabled(!latest_report_.isEmpty() && QFileInfo::exists(latest_report_));
    if (!capturing && !elapsed_.isValid())
        elapsed_label_->setText(tr("Not recording"));
    else if (!capturing)
        updateElapsedTime();
}
