#pragma once

#include "Memory.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hyprtoolkit {
    class IBackend;
    class ISystemIconDescription;
    class CImageElement;
    class CRectangleElement;
    class CRowLayoutElement;
    class CTextElement;
}

// remembers which name of an icon fallback list exists in the current theme
class CIconCache {
  public:
    explicit CIconCache(const SP<Hyprtoolkit::IBackend>& backend);

    // the first existing icon, or null
    SP<Hyprtoolkit::ISystemIconDescription> lookup(const std::vector<std::string>& names);

  private:
    SP<Hyprtoolkit::IBackend>                                                m_backend;
    std::unordered_map<std::string, SP<Hyprtoolkit::ISystemIconDescription>> m_cache;
};

enum eRowState : uint8_t {
    ROW_STATE_NORMAL = 0,
    // the keyboard cursor, not selected
    ROW_STATE_CURSOR,
    ROW_STATE_SELECTED,
};

// icon and name, optionally followed by the size and modification time columns
class CListRow {
  public:
    static constexpr float SIZE_COLUMN_WIDTH     = 90.F;
    static constexpr float MODIFIED_COLUMN_WIDTH = 110.F;

    CListRow(const SP<Hyprtoolkit::IBackend>& backend, float height, bool details);

    const SP<Hyprtoolkit::CRectangleElement>& element() const;

    void setContent(const SP<Hyprtoolkit::ISystemIconDescription>& icon, const std::string& name, const std::string& size = "", const std::string& modified = "");
    void setState(eRowState state);
    void setHovered(bool hovered);

    // row height for the current font size
    static float defaultHeight();

  private:
    void                                    updateColor();

    SP<Hyprtoolkit::IBackend>               m_backend;
    SP<Hyprtoolkit::CRectangleElement>      m_background;
    SP<Hyprtoolkit::CRowLayoutElement>      m_layout;
    SP<Hyprtoolkit::CImageElement>          m_icon;
    SP<Hyprtoolkit::CTextElement>           m_name;
    SP<Hyprtoolkit::CTextElement>           m_size;
    SP<Hyprtoolkit::CTextElement>           m_modified;

    SP<Hyprtoolkit::ISystemIconDescription> m_lastIcon;
    std::string                             m_lastName, m_lastSize, m_lastModified;
    float                                   m_iconSize = 0.F;
    eRowState                               m_state    = ROW_STATE_NORMAL;
    bool                                    m_hovered  = false;
};
