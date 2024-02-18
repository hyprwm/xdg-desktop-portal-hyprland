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

#include "mainpicker.h"
#include "elidedbutton.h"

std::string execAndGet(const char* cmd) {
    std::array<char, 128>                    buffer;
    std::string                              result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
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

        try {
            result.push_back({TITLESTR, CLASSSTR, std::stoull(IDSTR)});
        } catch (std::exception& e) {
            // silent err
        }

        rolling = rolling.substr(TITLESEPPOS + 5);
    }

    return result;
}

int main(int argc, char* argv[]) {
    qputenv("QT_LOGGING_RULES", "qml=false");

    const char*  WINDOWLISTSTR = getenv("XDPH_WINDOW_SHARING_LIST");

    const auto   WINDOWLIST = getWindows(WINDOWLISTSTR);

    QApplication picker(argc, argv);
    pickerPtr = &picker;
    MainPicker w;
    mainPickerPtr = &w;

    QSettings* settings = new QSettings("/tmp/hypr/hyprland-share-picker.conf", QSettings::IniFormat);
    w.setGeometry(0, 0, settings->value("width").toInt(), settings->value("height").toInt());

    // get the tabwidget
    const auto TABWIDGET        = w.findChild<QTabWidget *>("tabWidget");
    const auto ALLOWTOKENBUTTON = w.findChild<QCheckBox *>("checkBox");

    const auto TAB1 = (QWidget*)TABWIDGET->children()[0];

    const auto SCREENS_SCROLL_AREA_CONTENTS =
        (QWidget*)TAB1->findChild<QWidget*>("screens")->findChild<QScrollArea*>("scrollArea")->findChild<QWidget*>("scrollAreaWidgetContents");

    const auto SCREENS_SCROLL_AREA_CONTENTS_LAYOUT = SCREENS_SCROLL_AREA_CONTENTS->layout();

    // add all screens
    const auto    SCREENS = picker.screens();

    constexpr int BUTTON_HEIGHT = 41;

    for (int i = 0; i < SCREENS.size(); ++i) {
        const auto   GEOMETRY = SCREENS[i]->geometry();

        QString      text = QString::fromStdString(std::string("Screen " + std::to_string(i) + " at " + std::to_string(GEOMETRY.x()) + ", " + std::to_string(GEOMETRY.y()) + " (" +
                                                          std::to_string(GEOMETRY.width()) + "x" + std::to_string(GEOMETRY.height()) + ") (") +
                                              SCREENS[i]->name().toStdString() + ")");
        QString outputName = SCREENS[i]->name();
        ElidedButton* button = new ElidedButton(text);
        button->setMinimumSize(0, BUTTON_HEIGHT);
        SCREENS_SCROLL_AREA_CONTENTS_LAYOUT->addWidget(button);

        QObject::connect(button, &QPushButton::clicked, [=]() {
            std::cout << "[SELECTION]";
            std::cout << (ALLOWTOKENBUTTON->isChecked() ? "r" : "");
            std::cout << "/";

            std::cout << "screen:" << outputName.toStdString() << "\n";

            settings->setValue("width", mainPickerPtr->width());
            settings->setValue("height", mainPickerPtr->height());
            settings->sync();

            pickerPtr->quit();
            return 0;
        });
    }

    QSpacerItem * SCREENS_SPACER = new QSpacerItem(0,10000, QSizePolicy::Expanding, QSizePolicy::Expanding);
    SCREENS_SCROLL_AREA_CONTENTS_LAYOUT->addItem(SCREENS_SPACER);

    // windows
    const auto WINDOWS_SCROLL_AREA_CONTENTS =
        (QWidget*)TAB1->findChild<QWidget*>("windows")->findChild<QScrollArea*>("scrollArea_2")->findChild<QWidget*>("scrollAreaWidgetContents_2");

    const auto WINDOWS_SCROLL_AREA_CONTENTS_LAYOUT = WINDOWS_SCROLL_AREA_CONTENTS->layout();

    // loop over them
    int windowIterator = 0;
    for (auto& window : WINDOWLIST) {
        QString      text = QString::fromStdString(window.clazz + ": " + window.name);

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

    QSpacerItem * WINDOWS_SPACER = new QSpacerItem(0,10000, QSizePolicy::Expanding, QSizePolicy::Expanding);
    WINDOWS_SCROLL_AREA_CONTENTS_LAYOUT->addItem(WINDOWS_SPACER);

    // lastly, region
    const auto   REGION_OBJECT = (QWidget*)TAB1->findChild<QWidget*>("region");
    const auto   REGION_LAYOUT = REGION_OBJECT->layout();

    QString      text = "Select region...";
    
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
