#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>

class NotificationWidget : public QFrame {
    Q_OBJECT
public:
    explicit NotificationWidget(const QString& title, const QString& text, QWidget* parent = nullptr);

signals:
    void closed(); // 当用户手动关闭或动画结束时发出

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLabel* m_lblIcon;
    QLabel* m_lblTitle;
    QLabel* m_lblText;
    QPushButton* m_btnClose;
};
