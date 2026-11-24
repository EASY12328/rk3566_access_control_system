#include "app/access_app.h"
#include "pin_dialog.h"
#include "user_panel.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDebug>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>
#include <exception>
#include <memory>
#include <utility>

namespace {
volatile std::sig_atomic_t stopping = 0;
void stopOnSignal(int) { stopping = 1; }

class VideoView : public QWidget {
public:
    FramePtr frame;
    VideoView()
    {
        setMinimumSize(120, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#111b2a"));
        if (!frame) {
            painter.setPen(QColor("#b3c4d8"));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待摄像头画面"));
            return;
        }
        // The shared frame owns the RGB bytes for the duration of painting.
        const QImage image(frame->pixels.data(), frame->width, frame->height,
                           frame->width * 3, QImage::Format_RGB888);
        QRect target(QPoint(0, 0), image.size().scaled(size(), Qt::KeepAspectRatio));
        target.moveCenter(rect().center());
        painter.drawImage(target, image);
    }
};

class FaceJob : public QThread {
public:
    explicit FaceJob(AccessApp &app) : app_(app) {}
    FramePtr frame;
    bool enrolling = false;
    FaceResult result;

protected:
    void run() override
    {
        result = app_.scanFrame(frame, enrolling, [this] { return isInterruptionRequested(); });
    }

private:
    AccessApp &app_;
};

class PreviewWindow : public QWidget {
public:
    explicit PreviewWindow(std::shared_ptr<AccessApp> app)
        : app_(std::move(app)), faceJob_(*app_), userError_(QString::fromStdString(app_->userError()))
    {
        setWindowTitle(QStringLiteral("人脸门禁"));
        setStyleSheet(
            "QWidget { background: #0b1220; color: #e5edf7; font-size: 16px; }"
            "QPushButton { background: #253951; border: 1px solid #45627d;"
            " border-radius: 8px; padding: 7px; }"
            "QPushButton:disabled { color: #9eacc0; background: #172234; border-color: #27364a; }"
            "QPushButton#capture:enabled { background: #08735f; border-color: #27a78b; }"
            "QPushButton#capture:pressed { background: #075546; }");
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(10, 10, 10, 10);
        auto *header = new QHBoxLayout;
        auto *title = new QLabel(QStringLiteral("人脸门禁"));
        title->setStyleSheet("font-size: 22px; font-weight: bold;");
        header->addWidget(title);
        header->addStretch();
        auto *exit = new QPushButton(QStringLiteral("退出"));
        exit->setMinimumHeight(44);
        header->addWidget(exit);
        root->addLayout(header);
        connect(exit, &QPushButton::clicked, this, &QWidget::close);

        auto *body = new QHBoxLayout;
        video_ = new VideoView;
        body->addWidget(video_, 3);
        sidePanel_ = new QStackedWidget(this);
        homeActions_ = new QWidget(sidePanel_);
        auto *actions = new QVBoxLayout(homeActions_);
        actions->setContentsMargins(0, 0, 0, 0);
        actions->addStretch();
        usersButton_ = new QPushButton(QStringLiteral("用户管理"));
        capture_ = new QPushButton(QStringLiteral("扫脸开门"));
        capture_->setObjectName("capture");
        passwordButton_ = new QPushButton(QStringLiteral("密码开门"));
        resetButton_ = new QPushButton(QStringLiteral("修改密码"));
        for (auto *button : {usersButton_, capture_, passwordButton_, resetButton_}) {
            button->setMinimumHeight(52);
            button->setEnabled(false);
            actions->addWidget(button);
        }
        actions->addStretch();
        userPanel_ = new UserPanel(sidePanel_);
        sidePanel_->addWidget(homeActions_);
        sidePanel_->addWidget(userPanel_);
        body->addWidget(sidePanel_, 1);
        root->addLayout(body, 1);
        status_ = new QLabel(QStringLiteral("正在启动…"));
        status_->setWordWrap(true);
        root->addWidget(status_);
        result_ = new QLabel(QStringLiteral("请选择开门方式"));
        result_->setWordWrap(true);
        result_->setStyleSheet("color: #aabdd4; font-size: 14px;");
        root->addWidget(result_);

        userPanel_->setUsers(app_->users());
        if (!userError_.isEmpty())
            result_->setText(userError_);
        if (const auto door = app_->doorStatus(); door.state == DoorState::Fault)
            result_->setText(QString::fromStdString(door.error));
        connect(capture_, &QPushButton::clicked, this, [this] { capture(false); });
        connect(usersButton_, &QPushButton::clicked, this,
                [this] { openPinDialog(PinDialog::Purpose::ManageUsers); });
        connect(passwordButton_, &QPushButton::clicked, this,
                [this] { openPinDialog(PinDialog::Purpose::Unlock); });
        connect(resetButton_, &QPushButton::clicked, this,
                [this] { openPinDialog(PinDialog::Purpose::Change); });
        connect(userPanel_, &UserPanel::addRequested, this, [this] { capture(true); });
        connect(userPanel_, &UserPanel::removeRequested, this, [this](int id) { confirmRemoval(id); });
        connect(userPanel_, &UserPanel::backRequested, this, [this] {
            sidePanel_->setCurrentWidget(homeActions_);
            result_->setText(QStringLiteral("请选择开门方式"));
        });
        connect(&faceJob_, &QThread::finished, this, [this] { showFaceResult(); });
        connect(&refresh_, &QTimer::timeout, this, [this] { updatePreview(); });
        fpsTimer_.start();
        refresh_.start(50);
    }

    ~PreviewWindow() override { stop(); }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        stop();
        event->accept();
    }

private:
    void openPinDialog(PinDialog::Purpose purpose)
    {
        if (busy(app_->doorStatus()))
            return;
        auto *dialog = new PinDialog(purpose, app_, this);
        pinDialog_ = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QDialog::accepted, this, [this, dialog, purpose] {
            if (closing_ || stopping)
                return;
            switch (purpose) {
            case PinDialog::Purpose::ManageUsers:
                sidePanel_->setCurrentWidget(userPanel_);
                if (!userError_.isEmpty())
                    result_->setText(userError_);
                else if (!app_->canEnroll())
                    result_->setText(QStringLiteral("名单已满，删除一个用户后可继续录入"));
                else
                    result_->setText(QStringLiteral("请正对摄像头，再点录入人脸"));
                break;
            case PinDialog::Purpose::Unlock:
                showDoorResult(QStringLiteral("密码正确"), {}, dialog->doorResult());
                break;
            case PinDialog::Purpose::Change:
                result_->setText(QStringLiteral("密码已修改"));
                break;
            }
        });
        dialog->open();
    }

    void confirmRemoval(int id)
    {
        if (busy(app_->doorStatus()) || !userError_.isEmpty() || sidePanel_->currentWidget() != userPanel_)
            return;
        const auto name = faceUserName(id);
        auto *dialog = new QMessageBox(QMessageBox::Question, QStringLiteral("删除用户"),
                                        QStringLiteral("确定删除 %1？").arg(name),
                                        QMessageBox::NoButton, this);
        auto *cancel = dialog->addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        auto *remove = dialog->addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
        dialog->setDefaultButton(cancel);
        dialog->setEscapeButton(cancel);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QMessageBox::buttonClicked, this, [this, remove, id, name](QAbstractButton *button) {
            if (button != remove || closing_ || stopping)
                return;
            std::string error;
            if (!app_->removeUser(id, error)) {
                result_->setText(QString::fromStdString(error));
                return;
            }
            userPanel_->setUsers(app_->users());
            result_->setText(QStringLiteral("已删除 %1").arg(name));
        });
        dialog->open();
    }

    void showDoorResult(const QString &reason, const QString &details, const DoorStatus &door)
    {
        const auto extra = details.isEmpty() ? QString() : QStringLiteral("\n") + details;
        if (door.state == DoorState::Open) {
            waitingRelock_ = true;
            result_->setText(reason + QStringLiteral("，3 秒后回锁") + extra);
        } else if (door.state == DoorState::Simulation) {
            result_->setText(reason + QStringLiteral(" · 允许开门（测试）") + extra);
        } else {
            const auto error = QString::fromStdString(door.error);
            result_->setText(error.isEmpty() ? QStringLiteral("舵机尚未就绪") : error);
        }
    }

    void showFaceResult()
    {
        faceJob_.wait();
        faceJob_.frame.reset();
        checking_ = false;
        if (closing_ || stopping)
            return;
        const auto &outcome = faceJob_.result;
        if (faceJob_.enrolling) {
            if (outcome.userId) {
                userPanel_->setUsers(app_->users());
                result_->setText(QStringLiteral("已录入 %1").arg(faceUserName(outcome.userId)));
            } else {
                result_->setText(QString::fromStdString(outcome.error));
            }
            return;
        }
        if (!outcome.compared) {
            result_->setText(QStringLiteral("拒绝开门 · ") + QString::fromStdString(outcome.error));
            return;
        }
        const auto details = QStringLiteral("相似度 %1  /  阈值 %2")
                                 .arg(QString::number(outcome.score), QString::number(app_->threshold()));
        if (outcome.allowed)
            showDoorResult(QStringLiteral("%1 已通过").arg(faceUserName(outcome.userId)), details,
                           outcome.door);
        else
            result_->setText(QStringLiteral("拒绝开门  ") + details);
    }

    void stop()
    {
        if (closing_)
            return;
        closing_ = true;
        refresh_.stop();
        faceJob_.requestInterruption();
        app_->stop();
        faceJob_.wait();
    }

    bool busy(const DoorStatus &door) const
    {
        return closing_ || checking_ || pinDialog_ || door.state == DoorState::Open;
    }

    void updatePreview()
    {
        if (stopping) {
            close();
            return;
        }
        const auto door = app_->doorStatus();
        if (waitingRelock_ && door.state != DoorState::Open) {
            waitingRelock_ = false;
            if (door.state == DoorState::Locked)
                result_->setText(result_->text() + QStringLiteral("\n已发送回锁指令"));
            else if (door.state == DoorState::Fault)
                result_->setText(QString::fromStdString(door.error));
        }
        std::string cameraStatus;
        const auto frame = app_->latestFrame(&cameraStatus);
        const bool fresh = frame && frame->fresh();
        if (fresh && frame->sequence != shown_) {
            video_->frame = frame;
            video_->update();
            shown_ = frame->sequence;
            ++displayed_;
        } else if (!fresh && video_->frame) {
            video_->frame.reset();
            video_->update();
        }
        if (fpsTimer_.elapsed() >= 1000) {
            fps_ = 1000.0 * displayed_ / fpsTimer_.restart();
            displayed_ = 0;
        }
        if (!fresh && frame)
            cameraStatus = "画面中断，等待新帧…";
        const auto status = QString::fromStdString(cameraStatus) +
                            QStringLiteral("  ·  预览 %1 fps").arg(fps_, 0, 'f', 1);
        if (status_->text() != status)
            status_->setText(status);
        const bool available = !busy(door);
        const bool canScan = fresh && frame->warmedUp && userError_.isEmpty();
        usersButton_->setEnabled(available);
        passwordButton_->setEnabled(available);
        resetButton_->setEnabled(available);
        capture_->setEnabled(available && canScan);
        userPanel_->setControls(canScan && app_->canEnroll(), !available);
    }

    void capture(bool enrolling)
    {
        if (busy(app_->doorStatus()) || enrolling != (sidePanel_->currentWidget() == userPanel_))
            return;
        const auto frame = app_->latestFrame();
        if (!frame || !frame->fresh() || !frame->warmedUp)
            return;
        faceJob_.frame = frame;
        faceJob_.enrolling = enrolling;
        checking_ = true;
        capture_->setEnabled(false);
        usersButton_->setEnabled(false);
        passwordButton_->setEnabled(false);
        resetButton_->setEnabled(false);
        userPanel_->setControls(false, true);
        result_->setText(enrolling ? QStringLiteral("正在录入人脸…")
                                   : QStringLiteral("正在比对人脸…"));
        faceJob_.start();
    }

    std::shared_ptr<AccessApp> app_;
    FaceJob faceJob_;
    const QString userError_;
    VideoView *video_ = nullptr;
    QStackedWidget *sidePanel_ = nullptr;
    QWidget *homeActions_ = nullptr;
    UserPanel *userPanel_ = nullptr;
    QPushButton *usersButton_ = nullptr;
    QPushButton *capture_ = nullptr;
    QPushButton *passwordButton_ = nullptr;
    QPushButton *resetButton_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *result_ = nullptr;
    QPointer<PinDialog> pinDialog_;
    QTimer refresh_;
    QElapsedTimer fpsTimer_;
    std::uint64_t shown_ = 0;
    int displayed_ = 0;
    double fps_ = 0.0;
    bool checking_ = false;
    bool waitingRelock_ = false;
    bool closing_ = false;
};

bool readOptions(QCommandLineParser &parser, AccessOptions &options)
{
    options.device = parser.value("device").toStdString();
    options.output = parser.value("output").toStdString();
    options.recognizer = parser.value("recognizer").toStdString();
    options.usersFile = parser.value("users-file").toStdString();
    options.pinFile = parser.value("pin-file").toStdString();
    const auto rotation = parser.value("rotation");
    if (rotation != "0" && rotation != "180") {
        qCritical() << "--rotation must be 0 or 180";
        return false;
    }
    options.rotate180 = rotation == "180";
    bool validThreshold = false;
    options.threshold = parser.value("threshold").toDouble(&validThreshold);
    if (!validThreshold || !(options.threshold > 0.0 && options.threshold <= 1.0)) {
        qCritical() << "--threshold must be a number greater than 0 and at most 1";
        return false;
    }
    if (parser.isSet("servo-lock-us") || parser.isSet("servo-open-us")) {
        bool validLock = false, validOpen = false;
        options.lockedUs = parser.value("servo-lock-us").toInt(&validLock);
        options.openUs = parser.value("servo-open-us").toInt(&validOpen);
        if (!validLock || !validOpen || !AccessApp::validServoPositions(options.lockedUs, options.openUs)) {
            qCritical().noquote()
                << QStringLiteral("Set both --servo-lock-us and --servo-open-us to different values within %1..%2")
                       .arg(AccessApp::minPulseUs()).arg(AccessApp::maxPulseUs());
            return false;
        }
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("access_preview");
    AccessOptions options;
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"device", "V4L2 camera path", "path", QString::fromStdString(options.device)});
    parser.addOption({"output", "Directory for latest.bmp", "dir", QString::fromStdString(options.output)});
    parser.addOption({"recognizer", "Directory containing recognition tools and models",
                      "dir", QString::fromStdString(options.recognizer)});
    parser.addOption({"users-file", "Path to the enrolled user list",
                      "path", QString::fromStdString(options.usersFile)});
    parser.addOption({"pin-file", "Path to the password verifier",
                      "path", QString::fromStdString(options.pinFile)});
    parser.addOption({"rotation", "Camera image rotation in degrees (0 or 180)", "degrees", "180"});
    parser.addOption({"threshold", "Demo access decision cosine threshold (0 < score <= 1)",
                      "score", QString::number(options.threshold)});
    parser.addOption({"servo-lock-us", "PWM8 pulse in microseconds for the locked position", "us"});
    parser.addOption({"servo-open-us", "PWM8 pulse in microseconds for the open position", "us"});
    parser.addOption(QCommandLineOption("windowed", "Run in a window"));
    parser.process(app);
    if (!readOptions(parser, options))
        return 2;

    std::signal(SIGINT, stopOnSignal);
    std::signal(SIGTERM, stopOnSignal);
    const int fontId = QFontDatabase::addApplicationFont(
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf");
    if (fontId >= 0) {
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            app.setFont(QFont(families.first()));
    }
    try {
        auto controller = std::make_shared<AccessApp>(std::move(options));
        std::string error;
        if (!controller->start(error)) {
            qCritical().noquote() << QString::fromStdString(error);
            return 1;
        }
        PreviewWindow window(controller);
        window.resize(800, 480);
        if (parser.isSet("windowed"))
            window.show();
        else
            window.showFullScreen();
        return app.exec();
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
}
