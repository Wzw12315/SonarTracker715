#include "NotificationWidget.h"
#include <QStyle>
#include <QApplication>
#include <QPropertyAnimation>

NotificationWidget::NotificationWidget(const QString& title, const QString& text, QWidget* parent)
    : QFrame(parent) {

    // 1. 设置整体样式 (复刻你的绿框风格)
    setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
    setLineWidth(2);
    // 使用 StyleSheet 设置绿色边框、圆角和背景
    setStyleSheet("NotificationWidget { "
                   "  border: 2px solid #2ecc71; " // 绿色边框
                   "  border-radius: 5px; "
                   "  background-color: #f0fff0; " // 极淡绿色背景
                   "} "
                   "QLabel { border: none; background: transparent; }"); // 确保内部 Label 不受影响

    // 2. 创建内部部件
    m_lblIcon = new QLabel(this);
    // 这里需要加载你的 info 图标，临时使用系统自带图标
    QIcon infoIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation);
    m_lblIcon->setPixmap(infoIcon.pixmap(24, 24));

    m_lblTitle = new QLabel(title, this);
    m_lblTitle->setStyleSheet("font-weight: bold; color: #27ae60; font-size: 14px;");

    m_lblText = new QLabel(text, this);
    m_lblText->setWordWrap(true);
    m_lblText->setStyleSheet("color: #333333; font-size: 12px;");

    m_btnClose = new QPushButton("×", this);
    m_btnClose->setFixedSize(20, 20);
    m_btnClose->setStyleSheet("QPushButton { border: none; color: #7f8c8d; font-size: 16px; font-weight: bold; } "
                                "QPushButton:hover { color: #c0392b; background-color: #fdd; border-radius: 10px; }");

    // 3. 布局
    QHBoxLayout* hLayoutTitle = new QHBoxLayout();
    hLayoutTitle->addWidget(m_lblIcon);
    hLayoutTitle->addWidget(m_lblTitle);
    hLayoutTitle->addStretch();
    hLayoutTitle->addWidget(m_btnClose);
    hLayoutTitle->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(hLayoutTitle);
    mainLayout->addWidget(m_lblText);
    mainLayout->setContentsMargins(10, 5, 10, 10);

    // 4. 连接信号
    connect(m_btnClose, &QPushButton::clicked, this, [this](){
        emit closed();
        this->deleteLater();
    });

    // 固定宽度，高度适配内容
    setFixedWidth(300);
    adjustSize();
}

void NotificationWidget::showEvent(QShowEvent* event) {
    QFrame::showEvent(event);
    // 这里可以添加淡入动画
}
