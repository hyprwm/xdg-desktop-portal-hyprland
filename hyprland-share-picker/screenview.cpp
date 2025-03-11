#include <QScreen>
#include <QResizeEvent>
#include <algorithm>
#include <iostream>

#include "screenview.h"
#include "elidedbutton.h"

void ScreenView::activate(QApplication* app, QCheckBox* allowToken, QSettings* settings, MainPicker* mainPicker) {
    const auto screens = app->screens();

    int        lMost, rMost, tMost, bMost;
    for (int i = 0; i < screens.size(); ++i) {
        const auto geometry = screens[i]->geometry();

        if (i) {
            lMost = std::min(lMost, geometry.x());
            tMost = std::min(tMost, geometry.y());
            rMost = std::max(rMost, geometry.x() + geometry.width());
            bMost = std::max(bMost, geometry.y() + geometry.height());
        } else {
            lMost = geometry.x();
            tMost = geometry.y();
            rMost = geometry.x() + geometry.width();
            bMost = geometry.y() + geometry.height();
        }
    }

    leftMost   = (double)lMost;
    rightMost  = (double)rMost;
    topMost    = (double)tMost;
    bottomMost = (double)bMost;

    for (int i = 0; i < screens.size(); ++i) {
        const auto geometry = screens[i]->geometry();

        QString    outputName = screens[i]->name();
        QString    text =
            QString::fromStdString(std::string("Screen " + std::to_string(i) + " (" + std::to_string(geometry.width()) + "x" + std::to_string(geometry.height()) + ") (") +
                                   outputName.toStdString() + ")");

        ElidedButton* button = new ElidedButton(text, this);

        QObject::connect(button, &QPushButton::clicked, [=]() {
            std::cout << "[SELECTION]";
            std::cout << (allowToken->isChecked() ? "r" : "");
            std::cout << "/";

            std::cout << "screen:" << outputName.toStdString() << "\n";

            settings->setValue("width", mainPicker->width());
            settings->setValue("height", mainPicker->height());
            settings->sync();

            app->quit();
            return 0;
        });

        buttons.append(button);
        displays.append(geometry);
    }
}

void ScreenView::resizeEvent(QResizeEvent* event) {
    const double  newWidth  = (double)event->size().width();
    const double  newHeight = (double)event->size().height();

    const double  msWidth  = rightMost - leftMost;
    const double  msHeight = bottomMost - topMost;

    const double  fact = std::min(newWidth / msWidth, newHeight / msHeight);

    const double  offset_x = (newWidth - msWidth * fact) / 2;
    const double  offset_y = (newHeight - msHeight * fact) / 2;

    constexpr int inset = 2;

    for (int i = 0; i < buttons.size(); i++) {
        QRect     screen = displays[i];

        const int widgetX      = (int)(fact * (screen.x() - leftMost) + offset_x) + inset;
        const int widgetY      = (int)(fact * (screen.y() - topMost) + offset_y) + inset;
        const int widgetWidth  = (int)(fact * screen.width()) - 2 * inset;
        const int widgetHeight = (int)(fact * screen.height()) - 2 * inset;

        buttons[i]->setGeometry(widgetX, widgetY, widgetWidth, widgetHeight);
    }
}
