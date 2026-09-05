#include "Picker.hpp"

#include "RegionPicker.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/core/Output.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Checkbox.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/window/Window.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <iostream>

using namespace Hyprtoolkit;
using namespace Hyprutils::Memory;

static constexpr float SOURCE_BUTTON_HEIGHT = 42.F;

static CDynamicSize    fullSize() {
    return {CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}};
}

static CSharedPointer<CTextElement> makeText(const CSharedPointer<IBackend>& backend, std::string text, CFontSize fontSize = {CFontSize::HT_FONT_TEXT}) {
    return CTextBuilder::begin()->text(std::move(text))->fontSize(std::move(fontSize))->color([backend] { return backend->getPalette()->m_colors.text; })->commence();
}

CPicker::CPicker(const CSharedPointer<IBackend>& backend, std::vector<SOutputEntry> outputs, std::vector<SWindowEntry> windows, bool allowTokenByDefault) :
    m_backend(backend), m_outputs(std::move(outputs)), m_windows(std::move(windows)), m_allowTokenByDefault(allowTokenByDefault) {
    for (const auto& toolkitOutput : m_backend->getOutputs()) {
        const auto MATCH = std::ranges::find_if(m_outputs, [&toolkitOutput](const auto& output) { return output.name == toolkitOutput->port(); });
        if (MATCH != m_outputs.end()) {
            MATCH->description = toolkitOutput->desc();
            continue;
        }

        m_outputs.emplace_back(SOutputEntry{
            .name        = toolkitOutput->port(),
            .description = toolkitOutput->desc(),
        });
    }
}

CPicker::~CPicker() = default;

bool CPicker::initialize() {
    m_window = CWindowBuilder::begin()
                   ->type(HT_WINDOW_TOPLEVEL)
                   ->preferredSize({600, 400})
                   ->minSize({500, 320})
                   ->maxSize({1280, 800})
                   ->resizable(true)
                   ->appTitle("Select what to share")
                   ->appClass("hyprland-share-picker")
                   ->commence();
    if (!m_window)
        return false;

    buildUI();

    m_window->m_events.closeRequest.listenStatic([this] { cancel(); });
    return true;
}

void CPicker::run() {
    m_window->open();
    m_backend->enterLoop();
}

void CPicker::buildUI() {
    const auto BACKGROUND = CRectangleBuilder::begin()->color([this] { return m_backend->getPalette()->m_colors.background; })->size(fullSize())->commence();
    m_window->m_rootElement->addChild(BACKGROUND);

    m_mainLayout = CColumnLayoutBuilder::begin()->size(fullSize())->gap(10)->commence();
    m_mainLayout->setMargin(14);
    BACKGROUND->addChild(m_mainLayout);

    m_mainLayout->addChild(makeText(m_backend, "Select what to share", {CFontSize::HT_FONT_H2}));
    m_mainLayout->addChild(makeText(m_backend, "Choose a monitor, window, or region to share."));

    buildTabs();

    m_contentHost = CNullBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 10.F}})->commence();
    m_contentHost->setGrow(true);
    m_mainLayout->addChild(m_contentHost);

    const auto TOKEN_ROW = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(8)->commence();
    m_allowToken         = CCheckboxBuilder::begin()->toggled(m_allowTokenByDefault)->commence();
    TOKEN_ROW->addChild(m_allowToken);
    const auto TOKEN_LABEL = makeText(m_backend, "Remember this choice for future requests");
    TOKEN_ROW->addChild(TOKEN_LABEL);
    m_mainLayout->addChild(TOKEN_ROW);

    const auto ACTION_ROW = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(8)->commence();
    const auto SPACER     = CNullBuilder::begin()->commence();
    SPACER->setGrow(true);
    ACTION_ROW->addChild(SPACER);
    ACTION_ROW->addChild(CButtonBuilder::begin()->label("Cancel")->onMainClick([this](CSharedPointer<CButtonElement>) { cancel(); })->commence());
    m_shareButton = CButtonBuilder::begin()->label("Share")->accent(true)->enabled(false)->onMainClick([this](CSharedPointer<CButtonElement>) { submit(); })->commence();
    ACTION_ROW->addChild(m_shareButton);
    m_mainLayout->addChild(ACTION_ROW);

    rebuildContent();
}

void CPicker::buildTabs() {
    const auto TAB_ROW = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 36.F}})->commence();

    static constexpr std::array<const char*, 3> LABELS = {"Monitors", "Windows", "Region"};
    m_tabButtons.reserve(LABELS.size());
    for (size_t i = 0; i < LABELS.size(); ++i) {
        auto button = CButtonBuilder::begin()
                          ->label(LABELS[i])
                          ->accent(i == m_activeTab)
                          ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F / LABELS.size(), 1.F}})
                          ->onMainClick([this, i](CSharedPointer<CButtonElement>) { switchTab(static_cast<ePickerTab>(i)); })
                          ->commence();
        m_tabButtons.emplace_back(button);
        TAB_ROW->addChild(button);
    }

    m_mainLayout->addChild(TAB_ROW);
}

void CPicker::rebuildContent() {
    m_contentHost->clearChildren();
    m_sourceButtons.clear();

    switch (m_activeTab) {
        case PICKER_TAB_OUTPUTS: buildOutputContent(); break;
        case PICKER_TAB_WINDOWS: buildWindowContent(); break;
        case PICKER_TAB_REGION: buildRegionContent(); break;
    }
}

void CPicker::buildOutputContent() {
    if (m_outputs.empty()) {
        const auto TEXT = makeText(m_backend, "No monitors are available.");
        TEXT->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
        TEXT->setPositionFlag(IElement::HT_POSITION_FLAG_CENTER, true);
        m_contentHost->addChild(TEXT);
        return;
    }

    const auto SCROLL = CScrollAreaBuilder::begin()->scrollY(true)->showScrollbar(true)->size(fullSize())->commence();
    const auto LIST   = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(6)->commence();
    SCROLL->addChild(LIST);
    m_contentHost->addChild(SCROLL);

    m_sourceButtons.reserve(m_outputs.size());
    for (size_t i = 0; i < m_outputs.size(); ++i) {
        const auto& OUTPUT = m_outputs[i];
        std::string label  = OUTPUT.name;
        if (!OUTPUT.description.empty() && OUTPUT.description != OUTPUT.name)
            label += ": " + OUTPUT.description;
        if (OUTPUT.hasGeometry)
            label += std::format("  ({}×{} at [{}, {}])", OUTPUT.width, OUTPUT.height, OUTPUT.x, OUTPUT.y);

        const auto ESCAPED_LABEL = escapeMarkup(label);
        auto       button        = CButtonBuilder::begin()
                                       ->label(std::string{ESCAPED_LABEL})
                                       ->alignText(HT_FONT_ALIGN_LEFT)
                                       ->ellipsize(true)
                                       ->accent(m_selectedSource == i && m_selection && m_selection->type == SELECTION_OUTPUT)
                                       ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, SOURCE_BUTTON_HEIGHT}})
                                       ->onMainClick([this, i](CSharedPointer<CButtonElement>) { selectOutput(i); })
                                       ->commence();
        m_sourceButtons.emplace_back(button);
        LIST->addChild(button);
    }
}

void CPicker::buildWindowContent() {
    if (m_windows.empty()) {
        const auto TEXT = makeText(m_backend, "No shareable windows are available.");
        TEXT->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
        TEXT->setPositionFlag(IElement::HT_POSITION_FLAG_CENTER, true);
        m_contentHost->addChild(TEXT);
        return;
    }

    const auto SCROLL = CScrollAreaBuilder::begin()->scrollY(true)->showScrollbar(true)->size(fullSize())->commence();
    const auto LIST   = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(6)->commence();
    SCROLL->addChild(LIST);
    m_contentHost->addChild(SCROLL);

    m_sourceButtons.reserve(m_windows.size());
    for (size_t i = 0; i < m_windows.size(); ++i) {
        const auto& WINDOW = m_windows[i];
        std::string label  = WINDOW.title.empty() ? WINDOW.windowClass : WINDOW.title;
        if (!WINDOW.windowClass.empty() && WINDOW.windowClass != label)
            label += ": " + WINDOW.windowClass;

        const auto ESCAPED_LABEL = escapeMarkup(label);
        auto       button        = CButtonBuilder::begin()
                                       ->label(std::string{ESCAPED_LABEL})
                                       ->alignText(HT_FONT_ALIGN_LEFT)
                                       ->ellipsize(true)
                                       ->accent(m_selectedSource == i && m_selection && m_selection->type == SELECTION_WINDOW)
                                       ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, SOURCE_BUTTON_HEIGHT}})
                                       ->onMainClick([this, i](CSharedPointer<CButtonElement>) { selectWindow(i); })
                                       ->commence();
        m_sourceButtons.emplace_back(button);
        LIST->addChild(button);
    }
}

void CPicker::buildRegionContent() {
    const auto LAYOUT = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(10)->commence();
    LAYOUT->setMargin(8);
    m_contentHost->addChild(LAYOUT);

    LAYOUT->addChild(makeText(m_backend, "Select an area of the screen. The picker will temporarily hide while you draw the region."));

    const bool HAS_OUTPUTS = !m_outputs.empty();
    if (!HAS_OUTPUTS)
        LAYOUT->addChild(makeText(m_backend, "Region selection is unavailable because no monitors were found."));

    if (m_selection && m_selection->type == SELECTION_REGION)
        LAYOUT->addChild(makeText(
            m_backend, std::format("Selected: {}, {}×{} at [{}, {}]", escapeMarkup(m_selection->output), m_selection->width, m_selection->height, m_selection->x, m_selection->y)));

    LAYOUT->addChild(CButtonBuilder::begin()
                         ->label(m_selection && m_selection->type == SELECTION_REGION ? "Choose another region…" : "Choose region…")
                         ->enabled(!m_regionSelecting && HAS_OUTPUTS)
                         ->onMainClick([this](CSharedPointer<CButtonElement>) { beginRegionSelection(); })
                         ->commence());
}

void CPicker::updateSelectionButtons() {
    for (size_t i = 0; i < m_sourceButtons.size(); ++i)
        m_sourceButtons[i]->rebuild()->accent(m_selectedSource == i)->commence();

    m_shareButton->setEnabled(m_selection.has_value());
}

void CPicker::switchTab(ePickerTab tab) {
    if (tab == m_activeTab)
        return;

    m_activeTab = tab;
    m_selection.reset();
    m_selectedSource.reset();
    m_shareButton->setEnabled(false);

    for (size_t i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons[i]->rebuild()->accent(i == m_activeTab)->commence();

    rebuildContent();
}

void CPicker::selectOutput(size_t index) {
    if (index >= m_outputs.size())
        return;

    m_selection = SSelection{
        .type   = SELECTION_OUTPUT,
        .output = m_outputs[index].name,
    };
    m_selectedSource = index;
    updateSelectionButtons();
}

void CPicker::selectWindow(size_t index) {
    if (index >= m_windows.size())
        return;

    m_selection = SSelection{
        .type     = SELECTION_WINDOW,
        .windowID = m_windows[index].id,
    };
    m_selectedSource = index;
    updateSelectionButtons();
}

void CPicker::beginRegionSelection() {
    if (m_regionSelecting)
        return;

    m_regionSelecting = true;
    m_window->close();
    m_backend->addIdle([this] {
        const auto REGION = selectRegion(m_outputs);
        m_regionSelecting = false;
        if (REGION)
            m_selection = *REGION;

        rebuildContent();
        m_shareButton->setEnabled(m_selection.has_value());
        m_window->open();
    });
}

void CPicker::submit() {
    if (!m_selection)
        return;

    std::cout << formatSelection(*m_selection, m_allowToken->state()) << std::flush;
    m_window->close();
    m_backend->destroy();
}

void CPicker::cancel() {
    m_window->close();
    m_backend->destroy();
}
