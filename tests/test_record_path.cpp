#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <iostream>
#include <cassert>

#include "src/widgets/record/recordwidget.h"
#include "src/core/flameshot.h"
#include "src/core/flameshotdaemon.h"
#include "src/utils/systemnotification.h"

// Stubs for isolated testing
static Flameshot* s_flameshot = nullptr;
Flameshot* Flameshot::instance() { return s_flameshot; }
void Flameshot::setExternalWidget(bool) {}
FlameshotDaemon* FlameshotDaemon::instance() { return nullptr; }

SystemNotification::SystemNotification(QObject* parent) : QObject(parent) {}
void SystemNotification::sendMessage(const QString&, const QString&, const QString&, const int) {}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString dir = RecordWidget::getVideoSaveDirectory();
    QString expectedSuffix = QStringLiteral("/Video/flameshot-plus");
    std::cout << "Testing directory: " << dir.toStdString() << std::endl;

    // Check directory path
    if (!dir.endsWith(expectedSuffix)) {
        std::cerr << "Assertion FAILED: Directory '" << dir.toStdString() 
                  << "' does not end with expected suffix '" << expectedSuffix.toStdString() << "'" << std::endl;
        return 1;
    }

    // Check file path generation
    QString filePath = RecordWidget::generateOutputFilePath();
    std::cout << "Testing file path: " << filePath.toStdString() << std::endl;
    if (!filePath.startsWith(dir)) {
        std::cerr << "Assertion FAILED: File path does not start with dir" << std::endl;
        return 2;
    }
    if (!filePath.endsWith(QStringLiteral(".mp4"))) {
        std::cerr << "Assertion FAILED: File path does not end with .mp4" << std::endl;
        return 3;
    }

    // Check directory existence on disk
    QDir targetDir(dir);
    if (!targetDir.exists()) {
        std::cerr << "Assertion FAILED: Target directory does not exist on disk" << std::endl;
        return 4;
    }

    std::cout << "test_record_path PASSED" << std::endl;
    return 0;
}
