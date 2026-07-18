#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QString>

namespace mulan::profiling {
struct ProfileSnapshot;
}

class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;
class QWidget;

class ProfilerWindow final : public QDialog {
    Q_OBJECT
public:
    explicit ProfilerWindow(QWidget* parent = nullptr);

private slots:
    void startCapture();
    void stopCapture();
    void openLatestReport();
    void openReportsDirectory();
    void updateElapsedTime();

private:
    QString reportsDirectory() const;
    void clearResults();
    void showResults(const mulan::profiling::ProfileSnapshot& snapshot);
    void updateControls();

    QLabel* status_label_ = nullptr;
    QLabel* elapsed_label_ = nullptr;
    QLabel* report_label_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* open_button_ = nullptr;
    QScrollArea* results_scroll_ = nullptr;
    QWidget* results_content_ = nullptr;
    QVBoxLayout* results_layout_ = nullptr;
    QLabel* empty_results_label_ = nullptr;
    QString latest_report_;
    QElapsedTimer elapsed_;
    QTimer* refresh_timer_ = nullptr;
};
