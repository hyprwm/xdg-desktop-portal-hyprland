#ifndef SCREENVIEW_H
#define SCREENVIEW_H

#include <QWidget>
#include <QRect>
#include <QApplication>
#include <QCheckBox>
#include <QSettings>

#include "mainpicker.h"

class ScreenView : public QWidget {
  private:
    QList<QWidget*> buttons;
    QList<QRect>    displays;

    double          leftMost;
    double          rightMost;
    double          topMost;
    double          bottomMost;

  public:
    explicit ScreenView(QWidget* parent = nullptr) : QWidget(parent) {}

    void activate(QApplication* app, QCheckBox* allowToken, QSettings* settings, MainPicker* mainPicker);

  protected:
    void resizeEvent(QResizeEvent* event) override;
};

#endif // SCREENVIEW_H
