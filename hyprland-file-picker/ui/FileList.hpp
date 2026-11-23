#pragma once

#include "ListRow.hpp"
#include "Memory.hpp"
#include "../Directory.hpp"

#include <hyprutils/signal/Signal.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Hyprtoolkit {
    class IBackend;
    class IElement;
    class CColumnLayoutElement;
    class CNullElement;
    class CScrollAreaElement;
    class CTextElement;
}

class CMimeDatabase;

/*
    A virtualized list: only enough rows to fill the viewport exist, and they are
    rebound to whichever entries are scrolled into view. Entry n always lives in
    pool slot n % pool size, so scrolling by one row rebinds a single row.
*/
class CFileList {
  public:
    CFileList(const SP<Hyprtoolkit::IBackend>& backend, const SP<CIconCache>& icons, const SP<CMimeDatabase>& mime);
    ~CFileList();

    SP<Hyprtoolkit::IElement> element() const;

    // clears the selection without emitting selectionChanged, and scrolls to the top.
    // The placeholder shows while there are no entries.
    void                           setEntries(const std::filesystem::path& dir, std::vector<SFileEntry> entries, const std::string& placeholder);
    const std::vector<SFileEntry>& entries() const;

    // with multiple, a click toggles the entry instead of selecting only it
    void                  setMultiple(bool multiple);

    std::vector<size_t>   selection() const;
    std::optional<size_t> cursor() const;

    // keyboard navigation, extend grows the selection from the last toggled entry
    void moveCursor(long delta, bool extend);
    // selects only this entry and scrolls to it
    void select(size_t index);

    struct {
        Hyprutils::Signal::CSignalT<> selectionChanged;
        // double clicked
        Hyprutils::Signal::CSignalT<size_t> activated;
    } m_events;

  private:
    struct SSlot {
        UP<CListRow>          row;
        std::optional<size_t> index;
        bool                  attached = false;
    };

    void                                     updateRows();
    void                                     addSlot();
    void                                     bind(SSlot& slot, size_t index);
    void                                     detach(SSlot& slot);
    void                                     click(size_t index);
    void                                     setCursor(size_t index, bool extend);
    void                                     selectOnly(size_t index);
    void                                     selectRange(size_t from, size_t to);
    void                                     refreshStates();
    void                                     ensureVisible(size_t index);
    eRowState                                stateFor(size_t index) const;
    SP<Hyprtoolkit::ISystemIconDescription>  iconFor(const SFileEntry& entry);

    SP<Hyprtoolkit::IBackend>                m_backend;
    SP<CIconCache>                           m_icons;
    SP<CMimeDatabase>                        m_mime;

    SP<Hyprtoolkit::CColumnLayoutElement>    m_root;
    SP<Hyprtoolkit::CNullElement>            m_viewport;
    SP<Hyprtoolkit::CScrollAreaElement>      m_scroll;
    SP<Hyprtoolkit::CNullElement>            m_content;
    SP<Hyprtoolkit::CTextElement>            m_placeholder;
    bool                                     m_placeholderShown = false;

    std::filesystem::path                    m_dir;
    std::vector<SFileEntry>                  m_entries;
    std::vector<std::optional<SFileDetails>> m_details;
    std::vector<bool>                        m_selected;
    std::optional<size_t>                    m_cursor;
    std::optional<size_t>                    m_anchor;

    std::vector<SSlot>                       m_slots;
    float                                    m_rowHeight = 0.F;

    bool                                     m_multiple = false;

    std::optional<size_t>                    m_lastClicked;
    std::chrono::steady_clock::time_point    m_lastClickTime;
};
