#include "pin_dialog.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

void PinDialog::Job::run()
{
    if (replace) {
        error = QString::fromStdString(app->replacePin(currentPin.toStdString(), pin.toStdString()));
    } else {
        auto result = app->verifyPin(pin.toStdString(), unlock);
        error = QString::fromStdString(result.error);
        door = std::move(result.door);
    }
    pin.fill('\0');
    pin.clear();
    currentPin.fill('\0');
    currentPin.clear();
}

PinDialog::PinDialog(Purpose purpose, std::shared_ptr<AccessApp> app, QWidget *parent)
    : QDialog(parent), purpose_(purpose), job_(std::move(app))
{
    if (purpose_ == Purpose::ManageUsers)
        setWindowTitle(QStringLiteral("管理员验证"));
    else if (purpose_ == Purpose::Unlock)
        setWindowTitle(QStringLiteral("密码开门"));
    else
        setWindowTitle(QStringLiteral("修改密码"));

    auto *layout = new QVBoxLayout(this);
    prompt_ = new QLabel(purpose_ == Purpose::Unlock ? QStringLiteral("输入开门密码")
                                                    : QStringLiteral("输入当前密码"), this);
    layout->addWidget(prompt_);
    notice_ = new QLabel(this);
    notice_->setWordWrap(true);
    notice_->setMinimumHeight(22);
    layout->addWidget(notice_);

    controls_ = new QWidget(this);
    auto *keyLayout = new QVBoxLayout(controls_);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(controls_);
    pin_ = new QLineEdit(controls_);
    pin_->setEchoMode(QLineEdit::Password);
    pin_->setMaxLength(6);
    pin_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,6}"), pin_));
    pin_->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhNoPredictiveText |
                              Qt::ImhNoAutoUppercase | Qt::ImhSensitiveData);
    pin_->setAttribute(Qt::WA_InputMethodEnabled, false); // This dialog has its own keypad.
    pin_->setMinimumHeight(38);
    keyLayout->addWidget(pin_);

    auto *keys = new QGridLayout;
    for (int digit = 1; digit <= 9; ++digit) {
        auto *key = new QPushButton(QString::number(digit), controls_);
        key->setMinimumSize(68, 42);
        key->setAutoDefault(false);
        keys->addWidget(key, (digit - 1) / 3, (digit - 1) % 3);
        connect(key, &QPushButton::clicked, this, [this, digit] {
            notice_->clear();
            pin_->insert(QString::number(digit));
        });
    }
    auto *clear = new QPushButton(QStringLiteral("清空"), controls_);
    auto *zero = new QPushButton(QStringLiteral("0"), controls_);
    auto *backspace = new QPushButton(QStringLiteral("退格"), controls_);
    for (auto *key : {clear, zero, backspace}) {
        key->setMinimumHeight(42);
        key->setAutoDefault(false);
    }
    keys->addWidget(clear, 3, 0);
    keys->addWidget(zero, 3, 1);
    keys->addWidget(backspace, 3, 2);
    keyLayout->addLayout(keys);
    connect(clear, &QPushButton::clicked, pin_, &QLineEdit::clear);
    connect(zero, &QPushButton::clicked, this, [this] { notice_->clear(); pin_->insert("0"); });
    connect(backspace, &QPushButton::clicked, pin_, &QLineEdit::backspace);

    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton(QStringLiteral("取消"), controls_);
    auto *confirm = new QPushButton(QStringLiteral("确定"), controls_);
    for (auto *button : {cancel, confirm}) {
        button->setMinimumHeight(42);
        button->setAutoDefault(false);
        buttons->addWidget(button);
    }
    keyLayout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &PinDialog::reject);
    connect(confirm, &QPushButton::clicked, this, [this] { submit(); });
    connect(pin_, &QLineEdit::returnPressed, this, [this] { submit(); });
    connect(&job_, &QThread::finished, this, [this] { finished(); });
    pin_->setFocus();
}

PinDialog::~PinDialog()
{
    job_.wait();
    currentPin_.fill('\0');
    newPin_.fill('\0');
}

void PinDialog::reject()
{
    if (!busy_)
        QDialog::reject();
}

void PinDialog::submit()
{
    if (busy_)
        return;
    auto pin = pin_->text().toLatin1();
    if (!AccessApp::validPin(pin.toStdString())) {
        notice_->setText(QStringLiteral("请输入 6 位数字"));
        return;
    }
    pin_->clear();
    if (step_ == Step::Current) {
        if (purpose_ == Purpose::Change)
            currentPin_ = pin;
        startJob(std::move(pin), false);
    } else if (step_ == Step::New) {
        newPin_ = std::move(pin);
        step_ = Step::Confirm;
        prompt_->setText(QStringLiteral("再输入一次新密码"));
        notice_->clear();
        pin_->setFocus();
    } else if (pin != newPin_) {
        newPin_.fill('\0');
        newPin_.clear();
        pin.fill('\0');
        step_ = Step::New;
        prompt_->setText(QStringLiteral("输入新密码"));
        notice_->setText(QStringLiteral("两次新密码不同，请重输"));
        pin_->setFocus();
    } else {
        newPin_.fill('\0');
        newPin_.clear();
        startJob(std::move(pin), true);
    }
}

void PinDialog::startJob(QByteArray pin, bool replace)
{
    busy_ = true;
    controls_->setEnabled(false);
    notice_->setText(replace ? QStringLiteral("正在保存密码…") : QStringLiteral("正在验证密码…"));
    job_.pin = std::move(pin);
    job_.replace = replace;
    job_.unlock = purpose_ == Purpose::Unlock;
    if (replace)
        job_.currentPin = currentPin_;
    job_.start();
}

void PinDialog::finished()
{
    job_.wait();
    busy_ = false;
    controls_->setEnabled(true);
    pin_->setFocus();
    if (!job_.error.isEmpty()) {
        notice_->setText(job_.error);
        if (job_.replace) {
            step_ = Step::Current;
            prompt_->setText(QStringLiteral("输入当前密码"));
        }
        currentPin_.fill('\0');
        currentPin_.clear();
        return;
    }
    if (purpose_ == Purpose::Change && step_ == Step::Current) {
        step_ = Step::New;
        prompt_->setText(QStringLiteral("输入新密码"));
        notice_->clear();
        return;
    }
    currentPin_.fill('\0');
    currentPin_.clear();
    accept();
}
