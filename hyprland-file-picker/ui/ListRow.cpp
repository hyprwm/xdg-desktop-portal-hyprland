#include "ListRow.hpp"
#include "../PickerOptions.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/system/Icons.hpp>

#include <algorithm>
#include <cmath>

using namespace Hyprtoolkit;

static SP<CTextElement> makeText(const SP<IBackend>& backend, float alpha, eFontAlignment align, CDynamicSize&& size) {
    return CTextBuilder::begin()
        ->text("")
        ->color([backend] { return backend->getPalette()->m_colors.text; })
        ->a(alpha)
        ->align(align)
        // rows are rebound while scrolling, a stale label for a frame would show the wrong file
        ->async(false)
        ->interactable(false)
        ->size(std::move(size))
        ->commence();
}

CIconCache::CIconCache(const SP<IBackend>& backend) : m_backend(backend) {
    ;
}

SP<ISystemIconDescription> CIconCache::lookup(const std::vector<std::string>& names) {
    if (names.empty())
        return nullptr;

    if (const auto IT = m_cache.find(names.front()); IT != m_cache.end())
        return IT->second;

    SP<ISystemIconDescription> found;
    for (const auto& name : names) {
        auto icon = m_backend->systemIcons()->lookupIcon(name);
        if (!icon || !icon->exists())
            continue;

        found = icon;
        break;
    }

    m_cache.emplace(names.front(), found);
    return found;
}

CListRow::CListRow(const SP<IBackend>& backend, float height, bool details) : m_backend(backend), m_iconSize(std::round(height * 0.65F)) {
    m_background = CRectangleBuilder::begin()
                       ->color([] { return CHyprColor{0.F, 0.F, 0.F, 0.F}; })
                       ->rounding(backend->getPalette()->m_vars.smallRounding)
                       ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, height}})
                       ->commence();
    m_background->setReceivesMouse(true);

    m_layout = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->gap(8)->commence();
    m_layout->setMargin(4);
    m_background->addChild(m_layout);

    m_icon = CImageBuilder::begin()->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {m_iconSize, m_iconSize}})->sync(true)->a(0.F)->commence();
    m_layout->addChild(m_icon);

    // takes whatever the fixed columns leave, ellipsized
    m_name = makeText(backend, 1.F, HT_FONT_ALIGN_LEFT, {CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}});
    m_name->setGrow(true);
    m_layout->addChild(m_name);

    if (!details)
        return;

    m_size     = makeText(backend, 0.6F, HT_FONT_ALIGN_LEFT, {CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {SIZE_COLUMN_WIDTH, 1.F}});
    m_modified = makeText(backend, 0.6F, HT_FONT_ALIGN_LEFT, {CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {MODIFIED_COLUMN_WIDTH, 1.F}});
    m_layout->addChild(m_size);
    m_layout->addChild(m_modified);
}

const SP<CRectangleElement>& CListRow::element() const {
    return m_background;
}

void CListRow::setContent(const SP<ISystemIconDescription>& icon, const std::string& name, const std::string& size, const std::string& modified) {
    if (icon != m_lastIcon) {
        m_lastIcon = icon;

        if (icon)
            m_icon->rebuild()->icon(icon)->a(1.F)->commence();
        else
            m_icon->rebuild()->a(0.F)->commence();
    }

    if (name != m_lastName) {
        m_lastName = name;
        m_name->setText(escapeMarkup(name));
    }

    if (m_size && size != m_lastSize) {
        m_lastSize = size;
        m_size->setText(escapeMarkup(size));
    }

    if (m_modified && modified != m_lastModified) {
        m_lastModified = modified;
        m_modified->setText(escapeMarkup(modified));
    }
}

void CListRow::setState(eRowState state) {
    if (state == m_state)
        return;

    m_state = state;
    updateColor();
}

void CListRow::setHovered(bool hovered) {
    if (hovered == m_hovered)
        return;

    m_hovered = hovered;
    updateColor();
}

float CListRow::defaultHeight() {
    return std::max(28.F, std::round(CFontSize{CFontSize::HT_FONT_TEXT}.ptSize() * 2.F + 6.F));
}

void CListRow::updateColor() {
    m_background->rebuild()
        ->color([backend = m_backend, state = m_state, hovered = m_hovered] {
            const auto& COLORS = backend->getPalette()->m_colors;

            auto        color = state == ROW_STATE_NORMAL ? COLORS.text : COLORS.accent;
            switch (state) {
                case ROW_STATE_SELECTED: color.a = 0.45F; break;
                case ROW_STATE_CURSOR: color.a = 0.18F; break;
                case ROW_STATE_NORMAL: color.a = hovered ? 0.07F : 0.F; break;
            }

            return color;
        })
        ->commence();
}
