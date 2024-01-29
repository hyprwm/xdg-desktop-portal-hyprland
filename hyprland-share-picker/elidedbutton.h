#ifndef ELIDEDBUTTON_H
#define ELIDEDBUTTON_H

#include <QPushButton>

class ElidedButton : public QPushButton
{
public:
    explicit ElidedButton(QWidget *parent = nullptr);
    explicit ElidedButton(const QString &text, QWidget *parent = nullptr);
    void setText(QString);

protected:
    void resizeEvent(QResizeEvent *);

private:
    void updateText();
    QString og_text;
};

#endif // ELIDEDBUTTON_H
