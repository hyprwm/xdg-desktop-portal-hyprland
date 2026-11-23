#pragma once

#include "Memory.hpp"
#include "../Directory.hpp"
#include "../PickerOptions.hpp"
#include "../Places.hpp"

#include <hyprutils/signal/Listener.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Hyprtoolkit {
    class IBackend;
    class IElement;
    class IWindow;
    class CButtonElement;
    class CCheckboxElement;
    class CRowLayoutElement;
    class CTextboxElement;
    class CTextElement;

    namespace Input {
        struct SKeyboardKeyEvent;
    }
}

class CFileList;
class CIconCache;
class CListRow;
class CMimeDatabase;

class CFilePicker {
  public:
    CFilePicker(const SP<Hyprtoolkit::IBackend>& backend, SPickerOptions options);
    ~CFilePicker();

    bool initialize();

    // runs until the dialog is done, nullopt when it was cancelled
    std::optional<SPickerResult> run();

  private:
    // what the user worked with last, decides what Enter does
    enum eInput : uint8_t {
        INPUT_NONE = 0,
        INPUT_LIST,
        INPUT_NAME,
    };

    void                      buildUI();
    void                      buildToolbar();
    SP<Hyprtoolkit::IElement> buildSidebar();
    SP<Hyprtoolkit::IElement> buildNameRow();
    SP<Hyprtoolkit::IElement> buildChoices();
    SP<Hyprtoolkit::IElement> buildFooter();
    void                      showToolbar(bool newFolder);

    void                      navigate(const std::filesystem::path& target, bool remember = true);
    void                      refresh();
    bool                      shown(const SFileEntry& entry) const;
    void                      goBack();
    void                      goForward();
    void                      goUp();
    void                      setShowHidden(bool show);

    void                      onKey(const Hyprtoolkit::Input::SKeyboardKeyEvent& event);
    void                      onEnter();
    void                      onActivated(size_t index);
    void                      onSelectionChanged();

    void                      openTypedPath();
    void                      accept();
    void                      finish(std::vector<std::filesystem::path> paths);
    void                      cancel();

    void                      openNewFolder();
    void                      createFolder();
    void                      closeNewFolder();

    // keeps the focused textbox from also handling a shortcut
    void                                   releaseTextFocus();
    void                                   setMessage(const std::string& message);
    void                                   updateNavigation();
    void                                   updateAccept();
    std::string                            defaultAcceptLabel() const;
    std::string                            windowTitle() const;
    std::filesystem::path                  initialDirectory() const;
    std::optional<std::filesystem::path>   resolve(std::string_view text) const;

    SP<Hyprtoolkit::IBackend>              m_backend;
    SPickerOptions                         m_options;
    SP<CMimeDatabase>                      m_mime;
    SP<CIconCache>                         m_icons;
    std::vector<SPlace>                    m_places;

    SP<Hyprtoolkit::IWindow>               m_window;
    UP<CFileList>                          m_list;
    std::vector<UP<CListRow>>              m_placeRows;
    SP<Hyprtoolkit::CRowLayoutElement>     m_toolbar;
    SP<Hyprtoolkit::CButtonElement>        m_backButton;
    SP<Hyprtoolkit::CButtonElement>        m_forwardButton;
    SP<Hyprtoolkit::CButtonElement>        m_upButton;
    SP<Hyprtoolkit::CButtonElement>        m_newFolderButton;
    SP<Hyprtoolkit::CTextboxElement>       m_pathBox;
    SP<Hyprtoolkit::CTextElement>          m_newFolderLabel;
    SP<Hyprtoolkit::CTextboxElement>       m_newFolderBox;
    SP<Hyprtoolkit::CButtonElement>        m_createFolderButton;
    SP<Hyprtoolkit::CButtonElement>        m_cancelFolderButton;
    SP<Hyprtoolkit::CTextboxElement>       m_nameBox;
    SP<Hyprtoolkit::CTextElement>          m_message;
    SP<Hyprtoolkit::CCheckboxElement>      m_hiddenCheckbox;
    SP<Hyprtoolkit::CButtonElement>        m_acceptButton;

    Hyprutils::Signal::CHyprSignalListener m_selectionListener;
    Hyprutils::Signal::CHyprSignalListener m_activatedListener;

    std::filesystem::path                  m_dir;
    // the path box shows this unless the user edited it
    std::string                          m_pathText;
    std::vector<SFileEntry>              m_allEntries;
    std::vector<std::filesystem::path>   m_history;
    size_t                               m_historyPos = 0;

    size_t                               m_filterIndex = 0;
    std::vector<std::string>             m_choiceValues;
    std::optional<std::filesystem::path> m_confirmOverwrite;
    std::string                          m_acceptLabelShown;
    eInput                               m_lastInput     = INPUT_NONE;
    bool                                 m_showHidden    = false;
    bool                                 m_newFolderOpen = false;

    std::optional<SPickerResult>         m_result;
};
