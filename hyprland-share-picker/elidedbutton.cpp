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

void ElidedButton::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    updateText();
}

void ElidedButton::updateText()
{
    QFontMetrics metrics(font());
    QString elided = metrics.elidedText(og_text, Qt::ElideRight, width() - 15);
    QPushButton::setText(elided);
}