#include "FileList.hpp"
#include "../MimeDatabase.hpp"
#include "../PickerOptions.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <hyprutils/memory/Casts.hpp>

#include <algorithm>
#include <cmath>

using namespace Hyprtoolkit;
using namespace Hyprutils::Memory;

static constexpr auto   DOUBLE_CLICK_TIME = std::chrono::milliseconds(400);

static SP<CTextElement> makeHeaderText(const SP<IBackend>& backend, std::string text, eFontAlignment align, CDynamicSize&& size) {
    return CTextBuilder::begin()
        ->text(std::move(text))
        ->fontSize({CFontSize::HT_FONT_SMALL})
        ->color([backend] { return backend->getPalette()->m_colors.text; })
        ->a(0.6F)
        ->align(align)
        ->size(std::move(size))
        ->commence();
}

CFileList::CFileList(const SP<IBackend>& backend, const SP<CIconCache>& icons, const SP<CMimeDatabase>& mime) :
    m_backend(backend), m_icons(icons), m_mime(mime), m_rowHeight(CListRow::defaultHeight()) {
    m_root = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->gap(2)->commence();

    // laid out like the rows, so the columns line up
    const auto HEADER = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(8)->commence();
    HEADER->setMargin(4);
    HEADER->addChild(CNullBuilder::begin()->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {std::round(m_rowHeight * 0.65F), 1.F}})->commence());

    const auto NAME = makeHeaderText(backend, "Name", HT_FONT_ALIGN_LEFT, {CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}});
    NAME->setGrow(true);
    HEADER->addChild(NAME);
    HEADER->addChild(makeHeaderText(backend, "Size", HT_FONT_ALIGN_LEFT, {CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {CListRow::SIZE_COLUMN_WIDTH, 1.F}}));
    HEADER->addChild(makeHeaderText(backend, "Modified", HT_FONT_ALIGN_LEFT, {CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {CListRow::MODIFIED_COLUMN_WIDTH, 1.F}}));
    m_root->addChild(HEADER);

    m_viewport = CNullBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 10.F}})->commence();
    m_viewport->setGrow(true);
    m_root->addChild(m_viewport);

    m_scroll = CScrollAreaBuilder::begin()->scrollY(true)->showScrollbar(true)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->commence();
    m_viewport->addChild(m_scroll);

    // as tall as all entries together, the rows are placed on it absolutely
    m_content = CNullBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 1.F}})->commence();
    m_scroll->addChild(m_content);

    // runs before the scroll area lays out its content, so rebinding here lands in the same frame
    m_scroll->setRepositioned([this] { updateRows(); });

    m_placeholder = CTextBuilder::begin()->text("")->color([backend] { return backend->getPalette()->m_colors.text; })->a(0.6F)->commence();
    m_placeholder->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    m_placeholder->setPositionFlag(IElement::HT_POSITION_FLAG_CENTER, true);
}

CFileList::~CFileList() = default;

SP<IElement> CFileList::element() const {
    return m_root;
}

void CFileList::setEntries(const std::filesystem::path& dir, std::vector<SFileEntry> entries, const std::string& placeholder) {
    m_dir     = dir;
    m_entries = std::move(entries);
    m_details.assign(m_entries.size(), std::nullopt);
    m_selected.assign(m_entries.size(), false);
    m_cursor.reset();
    m_anchor.reset();
    m_lastClicked.reset();

    for (auto& slot : m_slots) {
        slot.index.reset();
    }

    m_content->rebuild()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, std::max(1.F, m_entries.size() * m_rowHeight)}})->commence();
    m_scroll->setScroll({0.F, 0.F});

    const bool SHOW_PLACEHOLDER = m_entries.empty();
    if (SHOW_PLACEHOLDER)
        m_placeholder->setText(escapeMarkup(placeholder));

    if (SHOW_PLACEHOLDER != m_placeholderShown) {
        if (SHOW_PLACEHOLDER)
            m_viewport->addChild(m_placeholder);
        else
            m_viewport->removeChild(m_placeholder);

        m_placeholderShown = SHOW_PLACEHOLDER;
    }

    updateRows();
}

const std::vector<SFileEntry>& CFileList::entries() const {
    return m_entries;
}

void CFileList::setMultiple(bool multiple) {
    m_multiple = multiple;
}

std::vector<size_t> CFileList::selection() const {
    std::vector<size_t> selection;
    for (size_t i = 0; i < m_selected.size(); ++i) {
        if (m_selected[i])
            selection.emplace_back(i);
    }

    return selection;
}

std::optional<size_t> CFileList::cursor() const {
    return m_cursor;
}

void CFileList::moveCursor(long delta, bool extend) {
    if (m_entries.empty())
        return;

    const long LAST = sc<long>(m_entries.size()) - 1;

    // without a cursor, start from what's on screen rather than jumping away
    const long FIRST_VISIBLE = sc<long>(std::ceil(m_scroll->getCurrentScroll().y / m_rowHeight));
    const long LAST_VISIBLE  = sc<long>((m_scroll->getCurrentScroll().y + m_scroll->size().y) / m_rowHeight) - 1;
    const long START         = m_cursor ? sc<long>(*m_cursor) + delta : (delta > 0 ? FIRST_VISIBLE : LAST_VISIBLE);
    const long NEXT          = std::clamp(START, 0L, LAST);

    if (extend && m_multiple && m_anchor)
        selectRange(*m_anchor, NEXT);
    else
        selectOnly(NEXT);

    m_cursor = NEXT;
    ensureVisible(NEXT);
    refreshStates();
    m_events.selectionChanged.emit();
}

void CFileList::updateRows() {
    const float VIEWPORT = m_scroll->size().y;
    if (VIEWPORT <= 0.F)
        return;

    const size_t NEEDED = sc<size_t>(std::ceil(VIEWPORT / m_rowHeight)) + 1;
    if (NEEDED > m_slots.size()) {
        // the slot of an entry depends on the pool size
        for (auto& slot : m_slots) {
            slot.index.reset();
        }

        while (m_slots.size() < NEEDED) {
            addSlot();
        }
    }

    const size_t POOL  = m_slots.size();
    const size_t FIRST = sc<size_t>(std::max(0.0, m_scroll->getCurrentScroll().y) / m_rowHeight);
    for (size_t i = 0; i < POOL; ++i) {
        const size_t INDEX = FIRST + i;
        auto&        slot  = m_slots[INDEX % POOL];

        if (INDEX >= m_entries.size())
            detach(slot);
        else if (slot.index != INDEX)
            bind(slot, INDEX);
    }
}

void CFileList::addSlot() {
    const size_t SLOT = m_slots.size();
    auto&        slot = m_slots.emplace_back(SSlot{.row = makeUnique<CListRow>(m_backend, m_rowHeight, true)});

    const auto&  ELEMENT = slot.row->element();
    ELEMENT->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    ELEMENT->setMouseButton([this, SLOT](Input::eMouseButton button, bool down) {
        if (!down || button != Input::MOUSE_BUTTON_LEFT || !m_slots[SLOT].index)
            return;

        click(*m_slots[SLOT].index);
    });
    ELEMENT->setMouseEnter([this, SLOT](const Hyprutils::Math::Vector2D&) { m_slots[SLOT].row->setHovered(true); });
    ELEMENT->setMouseLeave([this, SLOT] { m_slots[SLOT].row->setHovered(false); });
}

void CFileList::bind(SSlot& slot, size_t index) {
    slot.index = index;

    const auto& ENTRY = m_entries[index];

    // only rows that get shown are ever stat'ed
    if (!m_details[index])
        m_details[index] = fileDetails(m_dir / ENTRY.name);

    const auto& DETAILS = *m_details[index];
    slot.row->setContent(iconFor(ENTRY), ENTRY.name, DETAILS.valid && !ENTRY.directory ? formatSize(DETAILS.size) : "", DETAILS.valid ? formatModified(DETAILS.modified) : "");
    slot.row->setState(stateFor(index));
    slot.row->setHovered(false);
    slot.row->element()->setAbsolutePosition({0.F, index * m_rowHeight});

    if (slot.attached)
        return;

    m_content->addChild(slot.row->element());
    slot.attached = true;
}

void CFileList::detach(SSlot& slot) {
    slot.index.reset();

    if (!slot.attached)
        return;

    m_content->removeChild(slot.row->element());
    slot.attached = false;
}

void CFileList::click(size_t index) {
    const auto NOW          = std::chrono::steady_clock::now();
    const bool DOUBLE_CLICK = m_lastClicked == index && NOW - m_lastClickTime < DOUBLE_CLICK_TIME;
    m_lastClicked           = DOUBLE_CLICK ? std::nullopt : std::optional{index};
    m_lastClickTime         = NOW;

    // the second click of a double click must not toggle the entry back off
    if (DOUBLE_CLICK)
        m_selected[index] = true;
    else if (m_multiple) {
        m_selected[index] = !m_selected[index];
        m_anchor          = index;
    } else
        selectOnly(index);

    m_cursor = index;
    refreshStates();
    m_events.selectionChanged.emit();

    // activating may replace the entries, so not from inside the row's own callback
    if (DOUBLE_CLICK)
        m_backend->addIdle([this, index] { m_events.activated.emit(index); });
}

void CFileList::selectOnly(size_t index) {
    std::ranges::fill(m_selected, false);
    m_selected[index] = true;
    m_anchor          = index;
}

void CFileList::selectRange(size_t from, size_t to) {
    std::ranges::fill(m_selected, false);
    for (size_t i = std::min(from, to); i <= std::max(from, to); ++i) {
        m_selected[i] = true;
    }
}

void CFileList::refreshStates() {
    for (auto& slot : m_slots) {
        if (slot.index)
            slot.row->setState(stateFor(*slot.index));
    }
}

void CFileList::ensureVisible(size_t index) {
    const float TOP      = index * m_rowHeight;
    const float VIEWPORT = m_scroll->size().y;
    auto        scroll   = m_scroll->getCurrentScroll();

    if (TOP < scroll.y)
        scroll.y = TOP;
    else if (TOP + m_rowHeight > scroll.y + VIEWPORT)
        scroll.y = TOP + m_rowHeight - VIEWPORT;
    else
        return;

    m_scroll->setScroll(scroll);
}

eRowState CFileList::stateFor(size_t index) const {
    if (m_selected[index])
        return ROW_STATE_SELECTED;

    return m_cursor == index ? ROW_STATE_CURSOR : ROW_STATE_NORMAL;
}

SP<ISystemIconDescription> CFileList::iconFor(const SFileEntry& entry) {
    if (entry.directory)
        return m_icons->lookup({"folder", "inode-directory"});

    return m_icons->lookup(m_mime->iconsForType(m_mime->typeForName(entry.name)));
}
