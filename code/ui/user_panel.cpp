#include "user_panel.h"
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScroller>
#include <QVBoxLayout>

QString faceUserName(int id)
{
    return QStringLiteral("人脸%1").arg(id);
}

UserPanel::UserPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    count_ = new QLabel(this);
    layout->addWidget(count_);
    list_ = new QListWidget(this);
    list_->setIconSize(QSize(42, 42));
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(list_->viewport(), QScroller::TouchGesture);
    list_->setStyleSheet("QListWidget { background: #172234; border: 1px solid #38516e; }"
                        "QListWidget::item { min-height: 48px; padding: 2px 6px; }");
    layout->addWidget(list_, 1);
    auto *hint = new QLabel(QStringLiteral("点击用户可删除"), this);
    hint->setStyleSheet("color: #aabdd4; font-size: 14px;");
    layout->addWidget(hint);
    add_ = new QPushButton(QStringLiteral("录入人脸"), this);
    back_ = new QPushButton(QStringLiteral("返回"), this);
    add_->setMinimumHeight(48);
    back_->setMinimumHeight(44);
    layout->addWidget(add_);
    layout->addWidget(back_);
    connect(add_, &QPushButton::clicked, this, &UserPanel::addRequested);
    connect(back_, &QPushButton::clicked, this, &UserPanel::backRequested);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int id = item->data(Qt::UserRole).toInt();
        if (id > 0)
            emit removeRequested(id);
    });
}

void UserPanel::setUsers(const std::vector<UserView> &users)
{
    list_->clear();
    count_->setText(QStringLiteral("已录入 %1 / %2").arg(static_cast<int>(users.size()))
                        .arg(AccessApp::maxUsers()));
    if (users.empty()) {
        auto *item = new QListWidgetItem(QStringLiteral("暂无用户"), list_);
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const auto &user : users) {
        auto *item = new QListWidgetItem(faceUserName(user.id), list_);
        item->setData(Qt::UserRole, user.id);
        if (!user.portraitRgb.empty()) {
            const int size = AccessApp::portraitSize();
            const QImage portrait(reinterpret_cast<const uchar *>(user.portraitRgb.data()),
                                  size, size, size * 3, QImage::Format_RGB888);
            item->setIcon(QIcon(QPixmap::fromImage(portrait.copy())));
        }
    }
}

void UserPanel::setControls(bool canAdd, bool busy)
{
    add_->setEnabled(canAdd && !busy);
    back_->setEnabled(!busy);
    list_->setEnabled(!busy);
}
