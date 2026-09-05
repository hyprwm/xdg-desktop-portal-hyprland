#pragma once

#include "PickerData.hpp"

#include <hyprutils/memory/SharedPtr.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Hyprtoolkit {
    class IBackend;
    class IWindow;
    class CButtonElement;
    class CCheckboxElement;
    class CColumnLayoutElement;
    class CNullElement;
}

class CPicker {
  public:
    CPicker(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IBackend>& backend, std::vector<SOutputEntry> outputs, std::vector<SWindowEntry> windows,
            bool allowTokenByDefault);
    ~CPicker();

    bool initialize();
    void run();

  private:
    enum ePickerTab : uint8_t {
        PICKER_TAB_OUTPUTS = 0,
        PICKER_TAB_WINDOWS,
        PICKER_TAB_REGION,
    };

    void                                                                        buildUI();
    void                                                                        buildTabs();
    void                                                                        rebuildContent();
    void                                                                        buildOutputContent();
    void                                                                        buildWindowContent();
    void                                                                        buildRegionContent();
    void                                                                        updateSelectionButtons();
    void                                                                        switchTab(ePickerTab tab);
    void                                                                        selectOutput(size_t index);
    void                                                                        selectWindow(size_t index);
    void                                                                        beginRegionSelection();
    void                                                                        submit();
    void                                                                        cancel();

    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IBackend>                    m_backend;
    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IWindow>                     m_window;
    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>        m_mainLayout;
    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CNullElement>                m_contentHost;
    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CCheckboxElement>            m_allowToken;
    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>              m_shareButton;
    std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>> m_tabButtons;
    std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>> m_sourceButtons;

    std::vector<SOutputEntry>                                                   m_outputs;
    std::vector<SWindowEntry>                                                   m_windows;
    std::optional<SSelection>                                                   m_selection;
    std::optional<size_t>                                                       m_selectedSource;
    ePickerTab                                                                  m_activeTab           = PICKER_TAB_OUTPUTS;
    bool                                                                        m_regionSelecting     = false;
    bool                                                                        m_allowTokenByDefault = false;
};
