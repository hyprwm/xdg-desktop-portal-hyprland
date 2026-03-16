#ifndef ELIDEDBUTTON_H
#define ELIDEDBUTTON_H

#include <QPushButton>
#include <QIcon>
#include <QSize>

class ElidedButton : public QPushButton
{
public:
    explicit ElidedButton(QWidget *parent = nullptr);
    explicit ElidedButton(const QString &text, QWidget *parent = nullptr);
    void setText(QString);
    void setIcon(const QIcon &icon);
    void setIconSize(const QSize &size);

protected:
    void resizeEvent(QResizeEvent *);

private:
    void updateText();
    QString og_text;
};

#endif // ELIDEDBUTTON_H
