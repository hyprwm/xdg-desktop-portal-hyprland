#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QPushButton>
#include <QScreen>
#include <QTabWidget>
#include <QWidget>
#include <QtDebug>
#include <QtWidgets>
#include <QSettings>
#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <hyprutils/os/Process.hpp>
using namespace Hyprutils::OS;

#include "mainpicker.h"
#include "elidedbutton.h"
#include "screenview.h"

std::string execAndGet(const char* cmd) {
    std::string command = cmd + std::string{" 2>&1"};
    CProcess    proc("/bin/sh", {"-c", cmd});

    if (!proc.runSync())
        return "error";

    return proc.stdOut();
}

QApplication* pickerPtr     = nullptr;
MainPicker*   mainPickerPtr = nullptr;

struct SWindowEntry {
    std::string        name;
    std::string        clazz;
    unsigned long long id = 0;
};

std::vector<SWindowEntry> getWindows(const char* env) {
    std::vector<SWindowEntry> result;

    if (!env)
        return result;

    std::string rolling = env;

    while (!rolling.empty()) {
        // ID
        const auto IDSEPPOS = rolling.find("[HC>]");
        const auto IDSTR    = rolling.substr(0, IDSEPPOS);

        // class
        const auto CLASSSEPPOS = rolling.find("[HT>]");
        const auto CLASSSTR    = rolling.substr(IDSEPPOS + 5, CLASSSEPPOS - IDSEPPOS - 5);

        // title
        const auto TITLESEPPOS = rolling.find("[HE>]");
        const auto TITLESTR    = rolling.substr(CLASSSEPPOS + 5, TITLESEPPOS - 5 - CLASSSEPPOS);

        // window address
        const auto WINDOWSEPPOS = rolling.find("[HA>]");
        const auto WINDOWADDR = rolling.substr(TITLESEPPOS + 5, WINDOWSEPPOS - 5 - TITLESEPPOS);

        try {
            result.push_back({TITLESTR, CLASSSTR, std::stoull(IDSTR)});
        } catch (std::exception& e) {
            // silent err
        }

        rolling = rolling.substr(WINDOWSEPPOS + 5);
    }

    return result;
}

int main(int argc, char* argv[]) {
    qputenv("QT_LOGGING_RULES", "qml=false");

    bool allowTokenByDefault = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string{"--allow-token"})
            allowTokenByDefault = true;
    }

    const char*  WINDOWLISTSTR = getenv("XDPH_WINDOW_SHARING_LIST");
    const auto   WINDOWLIST    = getWindows(WINDOWLISTSTR);

    QApplication picker(argc, argv);
    pickerPtr = &picker;
    MainPicker w;
    mainPickerPtr = &w;

    QSettings* settings = new QSettings("/tmp/hypr/hyprland-share-picker.conf", QSettings::IniFormat);
    w.setGeometry(0, 0, settings->value("width").toInt(), settings->value("height").toInt());

    QCoreApplication::setApplicationName("org.hyprland.xdg-desktop-portal-hyprland");

    // get the tabwidget
    const auto TABWIDGET        = w.findChild<QTabWidget*>("tabWidget");
    const auto ALLOWTOKENBUTTON = w.findChild<QCheckBox*>("checkBox");

    if (allowTokenByDefault)
        ALLOWTOKENBUTTON->setCheckState(Qt::CheckState::Checked);

    const auto TAB1 = (QWidget*)TABWIDGET->children()[0];

    const auto SCREENS_WIDGET = (QWidget*)TAB1->findChild<QWidget*>("screens");

    auto       SCREENVIEW = new ScreenView();
    SCREENS_WIDGET->layout()->addWidget(SCREENVIEW);
    SCREENVIEW->activate(pickerPtr, ALLOWTOKENBUTTON, settings, mainPickerPtr);
    SCREENVIEW->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // add all screens
    const auto    SCREENS = picker.screens();

    constexpr int BUTTON_HEIGHT = 41;

    // windows
    const auto WINDOWS_SCROLL_AREA_CONTENTS =
        (QWidget*)TAB1->findChild<QWidget*>("windows")->findChild<QScrollArea*>("scrollArea_2")->findChild<QWidget*>("scrollAreaWidgetContents_2");

    const auto WINDOWS_SCROLL_AREA_CONTENTS_LAYOUT = WINDOWS_SCROLL_AREA_CONTENTS->layout();

    // loop over them
    int windowIterator = 0;
    for (auto& window : WINDOWLIST) {
        QString       text = QString::fromStdString(window.clazz + ": " + window.name);

        ElidedButton* button = new ElidedButton(text);
        button->setMinimumSize(0, BUTTON_HEIGHT);
        WINDOWS_SCROLL_AREA_CONTENTS_LAYOUT->addWidget(button);

        mainPickerPtr->windowIDs[button] = window.id;

        QObject::connect(button, &QPushButton::clicked, [=]() {
            std::cout << "[SELECTION]";
            std::cout << (ALLOWTOKENBUTTON->isChecked() ? "r" : "");
            std::cout << "/";

            std::cout << "window:" << mainPickerPtr->windowIDs[button] << "\n";

            settings->setValue("width", mainPickerPtr->width());
            settings->setValue("height", mainPickerPtr->height());
            settings->sync();

            pickerPtr->quit();
            return 0;
        });

        windowIterator++;
    }

    QSpacerItem* WINDOWS_SPACER = new QSpacerItem(0, 10000, QSizePolicy::Expanding, QSizePolicy::Expanding);
    WINDOWS_SCROLL_AREA_CONTENTS_LAYOUT->addItem(WINDOWS_SPACER);

    // lastly, region
    const auto    REGION_OBJECT = (QWidget*)TAB1->findChild<QWidget*>("region");
    const auto    REGION_LAYOUT = REGION_OBJECT->layout();

    QString       text = "Select region...";

    ElidedButton* button = new ElidedButton(text);
    button->setMaximumSize(400, BUTTON_HEIGHT);
    REGION_LAYOUT->addWidget(button);

    QObject::connect(button, &QPushButton::clicked, [=]() {
        auto REGION = execAndGet("slurp -f \"%o %x %y %w %h\"");
        REGION      = REGION.substr(0, REGION.length());

        // now, get the screen
        QScreen* pScreen = nullptr;
        if (REGION.find_first_of(' ') == std::string::npos) {
            std::cout << "error1\n";
            pickerPtr->quit();
            return 1;
        }
        const auto SCREEN_NAME = REGION.substr(0, REGION.find_first_of(' '));

        for (auto& screen : SCREENS) {
            if (screen->name().toStdString() == SCREEN_NAME) {
                pScreen = screen;
                break;
            }
        }

        if (!pScreen) {
            std::cout << "error2\n";
            pickerPtr->quit();
            return 1;
        }

        // get all the coords
        try {
            REGION       = REGION.substr(REGION.find_first_of(' ') + 1);
            const auto X = std::stoi(REGION.substr(0, REGION.find_first_of(' ')));
            REGION       = REGION.substr(REGION.find_first_of(' ') + 1);
            const auto Y = std::stoi(REGION.substr(0, REGION.find_first_of(' ')));
            REGION       = REGION.substr(REGION.find_first_of(' ') + 1);
            const auto W = std::stoi(REGION.substr(0, REGION.find_first_of(' ')));
            REGION       = REGION.substr(REGION.find_first_of(' ') + 1);
            const auto H = std::stoi(REGION);

            std::cout << "[SELECTION]";
            std::cout << (ALLOWTOKENBUTTON->isChecked() ? "r" : "");
            std::cout << "/";

            std::cout << "region:" << SCREEN_NAME << "@" << X - pScreen->geometry().x() << "," << Y - pScreen->geometry().y() << "," << W << "," << H << "\n";

            settings->setValue("width", mainPickerPtr->width());
            settings->setValue("height", mainPickerPtr->height());
            settings->sync();

            pickerPtr->quit();
            return 0;
        } catch (...) {
            std::cout << "error3\n";
            pickerPtr->quit();
            return 1;
        }

        std::cout << "error4\n";
        pickerPtr->quit();
        return 1;
    });

    w.show();
    return picker.exec();
}
