#pragma once
#include "app/access_app.h"
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
QString faceUserName(int id);

class UserPanel : public QWidget {
    Q_OBJECT
public:
    explicit UserPanel(QWidget *parent = nullptr);
    void setUsers(const std::vector<UserView> &users);
    void setControls(bool canAdd, bool busy);

signals:
    void addRequested();
    void removeRequested(int userId);
    void backRequested();

private:
    QLabel *count_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *back_ = nullptr;
};
