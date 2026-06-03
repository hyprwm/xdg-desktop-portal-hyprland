#pragma once

#include "defines.hpp"

#include <hyprutils/signal/Signal.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/element/Element.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Checkbox.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "Parsers.hpp"

class CPicker {
  public:
    explicit CPicker(bool allowTokenByDefault);
    ~CPicker();

    int run();

  private:
    enum eSourceTab : uint8_t {
        TAB_SCREENS = 0,
        TAB_WINDOWS,
        TAB_REGION,
    };

    void                                   build();
    void                                   buildTabBar(const SP<Hyprtoolkit::IElement>& parent);
    void                                   switchTab(eSourceTab tab);
    void                                   populateScreens();
    void                                   populateWindows();
    void                                   populateRegion();
    void                                   emitAndQuit(const std::string& kind, const std::string& detail);
    void                                   quit();

    void                                   loadGeometry();
    void                                   saveGeometry() const;

    SP<Hyprtoolkit::IBackend>              m_backend;
    SP<Hyprtoolkit::IWindow>               m_window;
    SP<Hyprtoolkit::CRowLayoutElement>     m_tabRow;
    SP<Hyprtoolkit::CColumnLayoutElement>  m_sourcesList;
    SP<Hyprtoolkit::CCheckboxElement>      m_restoreCheckbox;

    Hyprutils::Signal::CHyprSignalListener m_closeListener;
    Hyprutils::Signal::CHyprSignalListener m_resizeListener;

    std::vector<Picker::SMonitorEntry>     m_monitors;
    Hyprutils::Math::Vector2D              m_logicalSize{720, 600};
    eSourceTab                             m_activeTab    = TAB_SCREENS;
    bool                                   m_restoreToken = false;
};
