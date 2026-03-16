#include "elidedbutton.h"

ElidedButton::ElidedButton(QWidget *parent)
    : QPushButton(parent)
{
}

ElidedButton::ElidedButton( const QString& text, QWidget* parent )
    : ElidedButton( parent )
{
    setText(text);
}

void ElidedButton::setText(QString text)
{
    og_text = text;
    updateText();
}

void ElidedButton::setIcon(const QIcon& icon) {
    QPushButton::setIcon(icon);
    updateText();
}

void ElidedButton::setIconSize(const QSize& size) {
    QPushButton::setIconSize(size);
    updateText();
}

void ElidedButton::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    updateText();
}

void ElidedButton::updateText()
{
    QFontMetrics metrics(font());
    int          availableWidth = width() - 15;
    if (!icon().isNull())
        availableWidth -= iconSize().width() + 5;
    QString elided = metrics.elidedText(og_text, Qt::ElideRight, availableWidth);
    QPushButton::setText(elided);
}