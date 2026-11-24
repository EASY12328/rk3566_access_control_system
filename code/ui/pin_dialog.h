#pragma once
#include "app/access_app.h"
#include <QDialog>
#include <QThread>
#include <memory>
#include <utility>

class QLabel;
class QLineEdit;
class QWidget;

class PinDialog : public QDialog {
public:
    enum class Purpose { ManageUsers, Unlock, Change };
    PinDialog(Purpose purpose, std::shared_ptr<AccessApp> app, QWidget *parent);
    ~PinDialog() override;
    const DoorStatus &doorResult() const { return job_.door; }

public slots:
    void reject() override;

private:
    class Job : public QThread {
    public:
        explicit Job(std::shared_ptr<AccessApp> controller) : app(std::move(controller)) {}
        std::shared_ptr<AccessApp> app;
        QByteArray pin;
        QByteArray currentPin;
        QString error;
        bool replace = false;
        bool unlock = false;
        DoorStatus door;
        void run() override;
    };

    enum class Step { Current, New, Confirm };
    void submit();
    void finished();
    void startJob(QByteArray pin, bool replace);

    Purpose purpose_;
    Step step_ = Step::Current;
    Job job_;
    QByteArray currentPin_;
    QByteArray newPin_;
    QLabel *prompt_ = nullptr;
    QLineEdit *pin_ = nullptr;
    QLabel *notice_ = nullptr;
    QWidget *controls_ = nullptr;
    bool busy_ = false;
};
