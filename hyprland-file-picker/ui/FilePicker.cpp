#include "FilePicker.hpp"
#include "FileList.hpp"
#include "ListRow.hpp"
#include "../MimeDatabase.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/core/Timer.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Checkbox.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/window/Window.hpp>

#include <hyprutils/path/Path.hpp>

#include <cstdlib>
#include <format>

using namespace Hyprtoolkit;

static constexpr float TOOLBAR_HEIGHT = 30.F;
static constexpr float SIDEBAR_WIDTH  = 170.F;

// keysyms we handle, values from xkbcommon-keysyms.h
enum eKeysym : uint32_t {
    KEYSYM_H_UPPER   = 0x0048,
    KEYSYM_L_UPPER   = 0x004c,
    KEYSYM_H_LOWER   = 0x0068,
    KEYSYM_L_LOWER   = 0x006c,
    KEYSYM_BACKSPACE = 0xff08,
    KEYSYM_RETURN    = 0xff0d,
    KEYSYM_ESCAPE    = 0xff1b,
    KEYSYM_LEFT      = 0xff51,
    KEYSYM_UP        = 0xff52,
    KEYSYM_RIGHT     = 0xff53,
    KEYSYM_DOWN      = 0xff54,
    KEYSYM_KP_ENTER  = 0xff8d,
};

// a character being typed, not a shortcut or a control key like Return
static bool isPlainText(const Input::SKeyboardKeyEvent& event) {
    if (event.utf8.empty() || (event.modMask & (Input::HT_MODIFIER_CTRL | Input::HT_MODIFIER_ALT | Input::HT_MODIFIER_META)))
        return false;

    const auto FIRST = Hyprutils::Memory::sc<unsigned char>(event.utf8.front());
    return FIRST >= 0x20 && FIRST != 0x7f;
}

static CDynamicSize fullSize() {
    return {CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}};
}

static std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }

    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }

    return text;
}

// "/a/b/" and "/a/./b" both become "/a/b"
static std::filesystem::path normalizeDir(const std::filesystem::path& path) {
    auto normal = path.lexically_normal();
    if (normal.has_relative_path() && !normal.has_filename())
        normal = normal.parent_path();

    return normal;
}

static SP<CTextElement> makeLabel(const SP<IBackend>& backend, const std::string& text, float alpha = 1.F, CFontSize size = {CFontSize::HT_FONT_TEXT}) {
    return CTextBuilder::begin()->text(escapeMarkup(text))->fontSize(std::move(size))->color([backend] { return backend->getPalette()->m_colors.text; })->a(alpha)->commence();
}

CFilePicker::CFilePicker(const SP<IBackend>& backend, SPickerOptions options) :
    m_backend(backend), m_options(std::move(options)), m_mime(Hyprutils::Memory::makeShared<CMimeDatabase>()), m_icons(Hyprutils::Memory::makeShared<CIconCache>(backend)),
    m_places(defaultPlaces()), m_filterIndex(m_options.currentFilter) {
    m_mime->load(xdgDataDirs());

    for (auto& place : m_places) {
        place.path = normalizeDir(place.path);
    }

    for (const auto& choice : m_options.choices) {
        m_choiceValues.emplace_back(choice.initial);
    }
}

CFilePicker::~CFilePicker() = default;

bool CFilePicker::initialize() {
    m_window = CWindowBuilder::begin()
                   ->type(HT_WINDOW_TOPLEVEL)
                   ->preferredSize({860, 540})
                   ->minSize({560, 360})
                   ->resizable(true)
                   ->appTitle(windowTitle())
                   ->appClass("hyprland-file-picker")
                   ->commence();
    if (!m_window)
        return false;

    buildUI();

    m_window->m_events.closeRequest.listenStatic([this] { cancel(); });
    m_window->m_events.keyboardKey.listenStatic([this](const Input::SKeyboardKeyEvent& event) { onKey(event); });

    // the press is still being dispatched to whatever is hovered, navigate after it
    m_window->m_events.mouseButton.listenStatic([this](Input::eMouseButton button, bool down) {
        if (!down)
            return;

        if (button == Input::MOUSE_BUTTON_BACK)
            m_backend->addIdle([this] { goBack(); });
        else if (button == Input::MOUSE_BUTTON_FORWARD)
            m_backend->addIdle([this] { goForward(); });
    });

    navigate(initialDirectory());
    return true;
}

std::optional<SPickerResult> CFilePicker::run() {
    m_window->open();

    if (m_nameBox)
        m_nameBox->focus();

    m_backend->enterLoop();
    return m_result;
}

void CFilePicker::buildUI() {
    const auto BACKGROUND = CRectangleBuilder::begin()->color([this] { return m_backend->getPalette()->m_colors.background; })->size(fullSize())->commence();
    m_window->m_rootElement->addChild(BACKGROUND);

    const auto LAYOUT = CColumnLayoutBuilder::begin()->size(fullSize())->gap(8)->commence();
    LAYOUT->setMargin(10);
    BACKGROUND->addChild(LAYOUT);

    buildToolbar();
    LAYOUT->addChild(m_toolbar);

    const auto BODY = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 10.F}})->gap(8)->commence();
    BODY->setGrow(true);
    BODY->addChild(buildSidebar());

    const auto LIST_BACKGROUND = CRectangleBuilder::begin()
                                     ->color([this] { return m_backend->getPalette()->m_colors.base; })
                                     ->rounding(m_backend->getPalette()->m_vars.smallRounding)
                                     ->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {10.F, 1.F}})
                                     ->commence();
    LIST_BACKGROUND->setGrow(true);

    m_list = Hyprutils::Memory::makeUnique<CFileList>(m_backend, m_icons, m_mime);
    m_list->setMultiple(m_options.multiple);
    m_list->element()->setMargin(4);
    LIST_BACKGROUND->addChild(m_list->element());
    BODY->addChild(LIST_BACKGROUND);
    LAYOUT->addChild(BODY);

    m_selectionListener = m_list->m_events.selectionChanged.listen([this] { onSelectionChanged(); });
    m_activatedListener = m_list->m_events.activated.listen([this](size_t index) { onActivated(index); });

    m_message = makeLabel(m_backend, "", 0.7F, {CFontSize::HT_FONT_SMALL});
    LAYOUT->addChild(m_message);

    if (m_options.mode == PICKER_MODE_SAVE)
        LAYOUT->addChild(buildNameRow());

    if (!m_options.choices.empty())
        LAYOUT->addChild(buildChoices());

    LAYOUT->addChild(buildFooter());
}

void CFilePicker::buildToolbar() {
    m_toolbar = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, TOOLBAR_HEIGHT}})->gap(6)->commence();

    // the toolbar swaps its contents, so its buttons act once their click is done
    const auto BUTTON = [this](std::string&& label, std::function<void()>&& action, bool square = false) {
        return CButtonBuilder::begin()
            ->label(std::move(label))
            ->size(square ? CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {TOOLBAR_HEIGHT + 4.F, 1.F}} :
                            CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})
            ->onMainClick([this, action = std::move(action)](SP<CButtonElement>) { m_backend->addIdle(action); })
            ->commence();
    };

    m_backButton    = BUTTON("←", [this] { goBack(); }, true);
    m_forwardButton = BUTTON("→", [this] { goForward(); }, true);
    m_upButton      = BUTTON("↑", [this] { goUp(); }, true);

    m_pathBox = CTextboxBuilder::begin()->multiline(false)->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {10.F, 1.F}})->commence();
    m_pathBox->setGrow(true);

    if (m_options.mode != PICKER_MODE_OPEN)
        m_newFolderButton = BUTTON("New Folder", [this] { openNewFolder(); });

    m_newFolderLabel = makeLabel(m_backend, "New folder:");
    m_newFolderBox =
        CTextboxBuilder::begin()->placeholder("Folder name")->multiline(false)->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {10.F, 1.F}})->commence();
    m_newFolderBox->setGrow(true);
    m_cancelFolderButton = BUTTON("Cancel", [this] { closeNewFolder(); });
    m_createFolderButton = BUTTON("Create", [this] { createFolder(); });
    m_createFolderButton->rebuild()->accent(true)->commence();

    showToolbar(false);
}

void CFilePicker::showToolbar(bool newFolder) {
    m_toolbar->clearChildren();

    if (newFolder) {
        m_toolbar->addChild(m_newFolderLabel);
        m_toolbar->addChild(m_newFolderBox);
        m_toolbar->addChild(m_cancelFolderButton);
        m_toolbar->addChild(m_createFolderButton);
        return;
    }

    m_toolbar->addChild(m_backButton);
    m_toolbar->addChild(m_forwardButton);
    m_toolbar->addChild(m_upButton);
    m_toolbar->addChild(m_pathBox);

    if (m_newFolderButton)
        m_toolbar->addChild(m_newFolderButton);
}

SP<IElement> CFilePicker::buildSidebar() {
    const auto SIDEBAR = CColumnLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {SIDEBAR_WIDTH, 1.F}})->gap(2)->commence();

    for (size_t i = 0; i < m_places.size(); ++i) {
        auto row = Hyprutils::Memory::makeUnique<CListRow>(m_backend, CListRow::defaultHeight(), false);
        row->setContent(m_icons->lookup({m_places[i].icon, "folder"}), m_places[i].label);

        const auto& ELEMENT = row->element();
        ELEMENT->setMouseButton([this, i](Input::eMouseButton button, bool down) {
            if (down && button == Input::MOUSE_BUTTON_LEFT)
                navigate(m_places[i].path);
        });
        ELEMENT->setMouseEnter([this, i](const Hyprutils::Math::Vector2D&) { m_placeRows[i]->setHovered(true); });
        ELEMENT->setMouseLeave([this, i] { m_placeRows[i]->setHovered(false); });

        SIDEBAR->addChild(ELEMENT);
        m_placeRows.emplace_back(std::move(row));
    }

    return SIDEBAR;
}

SP<IElement> CFilePicker::buildNameRow() {
    const auto ROW = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, TOOLBAR_HEIGHT}})->gap(8)->commence();
    ROW->addChild(makeLabel(m_backend, "Name:"));

    m_nameBox = CTextboxBuilder::begin()
                    ->defaultText(std::string{m_options.name})
                    ->placeholder("File name")
                    ->multiline(false)
                    ->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {10.F, 1.F}})
                    ->onTextEdited([this](SP<CTextboxElement>, const std::string&) {
                        m_lastInput = INPUT_NAME;
                        m_confirmOverwrite.reset();
                        updateAccept();
                    })
                    ->commence();
    m_nameBox->setGrow(true);
    ROW->addChild(m_nameBox);

    return ROW;
}

SP<IElement> CFilePicker::buildChoices() {
    const auto ROW = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(16)->commence();

    for (size_t i = 0; i < m_options.choices.size(); ++i) {
        const auto& CHOICE = m_options.choices[i];
        const auto  ITEM   = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(6)->commence();

        if (CHOICE.options.empty()) {
            ITEM->addChild(CCheckboxBuilder::begin()
                               ->toggled(m_choiceValues[i] == "true")
                               ->onToggled([this, i](SP<CCheckboxElement>, bool toggled) { m_choiceValues[i] = toggled ? "true" : "false"; })
                               ->commence());
            ITEM->addChild(makeLabel(m_backend, CHOICE.label));
        } else {
            std::vector<std::string> items;
            for (const auto& option : CHOICE.options) {
                items.emplace_back(escapeMarkup(option));
            }

            ITEM->addChild(makeLabel(m_backend, CHOICE.label));
            ITEM->addChild(CComboboxBuilder::begin()
                               ->items(std::move(items))
                               ->currentItem(std::stoul(m_choiceValues[i]))
                               ->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {180.F, 28.F}})
                               ->onChanged([this, i](SP<CComboboxElement>, size_t index) { m_choiceValues[i] = std::to_string(index); })
                               ->commence());
        }

        ROW->addChild(ITEM);
    }

    return ROW;
}

SP<IElement> CFilePicker::buildFooter() {
    const auto FOOTER = CRowLayoutBuilder::begin()->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(8)->commence();

    m_hiddenCheckbox = CCheckboxBuilder::begin()->toggled(m_showHidden)->onToggled([this](SP<CCheckboxElement>, bool toggled) { setShowHidden(toggled); })->commence();
    FOOTER->addChild(m_hiddenCheckbox);
    FOOTER->addChild(makeLabel(m_backend, "Show hidden files"));

    const auto SPACER = CNullBuilder::begin()->commence();
    SPACER->setGrow(true);
    FOOTER->addChild(SPACER);

    if (!m_options.filters.empty()) {
        std::vector<std::string> names;
        for (const auto& filter : m_options.filters) {
            names.emplace_back(escapeMarkup(filter.name));
        }

        FOOTER->addChild(CComboboxBuilder::begin()
                             ->items(std::move(names))
                             ->currentItem(m_filterIndex)
                             ->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {200.F, 28.F}})
                             ->onChanged([this](SP<CComboboxElement>, size_t index) {
                                 m_filterIndex = index;
                                 refresh();
                                 updateAccept();
                             })
                             ->commence());
    }

    FOOTER->addChild(CButtonBuilder::begin()->label("Cancel")->onMainClick([this](SP<CButtonElement>) { cancel(); })->commence());

    m_acceptLabelShown = defaultAcceptLabel();
    m_acceptButton     = CButtonBuilder::begin()->label(escapeMarkup(m_acceptLabelShown))->accent(true)->onMainClick([this](SP<CButtonElement>) { accept(); })->commence();
    FOOTER->addChild(m_acceptButton);

    return FOOTER;
}

void CFilePicker::navigate(const std::filesystem::path& target, bool remember) {
    const auto DIR     = normalizeDir(target);
    auto       entries = listDirectory(DIR);
    if (!entries) {
        setMessage(std::format("Can't open {}: {}", DIR.string(), entries.error()));
        return;
    }

    if (remember) {
        if (!m_history.empty())
            m_history.resize(m_historyPos + 1);

        if (m_history.empty() || m_history.back() != DIR)
            m_history.emplace_back(DIR);

        m_historyPos = m_history.size() - 1;
    }

    m_dir        = DIR;
    m_allEntries = std::move(*entries);
    m_lastInput  = INPUT_NONE;
    m_confirmOverwrite.reset();

    setMessage("");
    refresh();
    updateNavigation();
}

void CFilePicker::refresh() {
    // the entries change under the search
    endQuickSearch();

    std::vector<SFileEntry> entries;
    for (const auto& entry : m_allEntries) {
        if (shown(entry))
            entries.emplace_back(entry);
    }

    const char* placeholder = m_allEntries.empty() ? "This folder is empty" : (m_options.mode == PICKER_MODE_DIRECTORY ? "No folders" : "No matching files");
    m_list->setEntries(m_dir, std::move(entries), placeholder);
}

bool CFilePicker::shown(const SFileEntry& entry) const {
    if (entry.hidden && !m_showHidden)
        return false;

    if (entry.directory)
        return true;

    if (m_options.mode == PICKER_MODE_DIRECTORY)
        return false;

    return m_options.filters.empty() || matchesFilter(m_options.filters[m_filterIndex], entry.name, *m_mime);
}

void CFilePicker::goBack() {
    if (m_historyPos == 0)
        return;

    --m_historyPos;
    navigate(m_history[m_historyPos], false);
}

void CFilePicker::goForward() {
    if (m_historyPos + 1 >= m_history.size())
        return;

    ++m_historyPos;
    navigate(m_history[m_historyPos], false);
}

void CFilePicker::goUp() {
    if (m_dir.has_relative_path())
        navigate(m_dir.parent_path());
}

void CFilePicker::setShowHidden(bool show) {
    if (show == m_showHidden)
        return;

    m_showHidden = show;
    if (m_hiddenCheckbox->state() != show)
        m_hiddenCheckbox->setState(show);

    refresh();
    updateAccept();
}

void CFilePicker::onKey(const Input::SKeyboardKeyEvent& event) {
    if (!event.down)
        return;

    const bool CTRL  = event.modMask & Input::HT_MODIFIER_CTRL;
    const bool ALT   = event.modMask & Input::HT_MODIFIER_ALT;
    const bool SHIFT = event.modMask & Input::HT_MODIFIER_SHIFT;

    // an active search takes Escape and Backspace before the dialog does
    if (onQuickSearchKey(event))
        return;

    switch (event.xkbKeysym) {
        case KEYSYM_ESCAPE:
            if (m_newFolderOpen)
                closeNewFolder();
            else
                cancel();
            break;

        case KEYSYM_RETURN:
        case KEYSYM_KP_ENTER: onEnter(); break;

        case KEYSYM_UP:
        case KEYSYM_DOWN:
            if (ALT) {
                if (event.xkbKeysym == KEYSYM_UP) {
                    releaseTextFocus();
                    goUp();
                }
                break;
            }

            // don't let the list overwrite a name that's being typed
            if (m_options.mode == PICKER_MODE_SAVE && m_lastInput == INPUT_NAME)
                break;

            m_list->moveCursor(event.xkbKeysym == KEYSYM_UP ? -1 : 1, SHIFT);
            break;

        case KEYSYM_LEFT:
        case KEYSYM_RIGHT:
            if (!ALT)
                break;

            releaseTextFocus();
            if (event.xkbKeysym == KEYSYM_LEFT)
                goBack();
            else
                goForward();
            break;

        case KEYSYM_H_LOWER:
        case KEYSYM_H_UPPER:
            if (!CTRL)
                break;

            releaseTextFocus();
            setShowHidden(!m_showHidden);
            break;

        case KEYSYM_L_LOWER:
        case KEYSYM_L_UPPER:
            if (!CTRL || m_newFolderOpen)
                break;

            // focusing now would hand this very key to the path box
            releaseTextFocus();
            m_backend->addIdle([this] { m_pathBox->focus(); });
            break;

        default: break;
    }
}

void CFilePicker::onEnter() {
    if (m_newFolderOpen) {
        createFolder();
        return;
    }

    if (m_pathBox->currentText() != m_pathText) {
        openTypedPath();
        return;
    }

    const auto CURSOR = m_list->cursor();
    if (m_lastInput == INPUT_LIST && CURSOR && m_list->entries()[*CURSOR].directory && m_list->selection().size() == 1) {
        navigate(m_dir / m_list->entries()[*CURSOR].name);
        return;
    }

    accept();
}

void CFilePicker::onActivated(size_t index) {
    if (index >= m_list->entries().size())
        return;

    const auto& ENTRY = m_list->entries()[index];
    if (ENTRY.directory) {
        navigate(m_dir / ENTRY.name);
        return;
    }

    accept();
}

void CFilePicker::onSelectionChanged() {
    // clicks and arrow keys start over
    if (!m_quickSearchSelecting)
        endQuickSearch();

    m_lastInput = INPUT_LIST;

    const auto CURSOR = m_list->cursor();
    if (m_options.mode == PICKER_MODE_SAVE && CURSOR && !m_list->entries()[*CURSOR].directory) {
        m_nameBox->setText(m_list->entries()[*CURSOR].name);
        m_confirmOverwrite.reset();
    }

    updateAccept();
}

bool CFilePicker::onQuickSearchKey(const Input::SKeyboardKeyEvent& event) {
    if (m_newFolderOpen || textFocused())
        return false;

    // don't let the list overwrite a name that's being typed
    if (m_options.mode == PICKER_MODE_SAVE && m_lastInput == INPUT_NAME)
        return false;

    const auto NOW = CQuickSearch::Clock::now();

    if (event.xkbKeysym == KEYSYM_ESCAPE || event.xkbKeysym == KEYSYM_BACKSPACE) {
        if (!m_quickSearch.active(NOW))
            return false;

        if (event.xkbKeysym == KEYSYM_ESCAPE)
            endQuickSearch();
        else
            showQuickSearch(m_quickSearch.backspace(m_list->entries(), m_list->cursor(), NOW));

        return true;
    }

    if (!isPlainText(event))
        return false;

    showQuickSearch(m_quickSearch.type(event.utf8, m_list->entries(), m_list->cursor(), NOW));
    return true;
}

void CFilePicker::showQuickSearch(std::optional<size_t> match) {
    if (m_quickSearch.query().empty()) {
        endQuickSearch();
        return;
    }

    // unlike a click, this selection mustn't end the search
    if (match) {
        m_quickSearchSelecting = true;
        m_list->select(*match);
        m_quickSearchSelecting = false;
    }

    // set after setMessage, which clears it for every other message
    setMessage(std::format("Find: {}{}", m_quickSearch.query(), match ? "" : " (no match)"));
    m_quickSearchShown = true;

    if (m_quickSearchTimer)
        m_quickSearchTimer->cancel();

    m_quickSearchTimer = m_backend->addTimer(CQuickSearch::TIMEOUT, [this](ASP<CTimer>, void*) { endQuickSearch(); }, nullptr);
}

void CFilePicker::endQuickSearch() {
    m_quickSearch.reset();

    if (m_quickSearchTimer) {
        m_quickSearchTimer->cancel();
        m_quickSearchTimer.reset();
    }

    // a message that replaced the query stays
    if (m_quickSearchShown)
        setMessage("");
}

void CFilePicker::openTypedPath() {
    const auto TEXT   = std::string{trim(m_pathBox->currentText())};
    const auto TARGET = resolve(TEXT);
    if (!TARGET) {
        setMessage(std::format("{} isn't a valid path", TEXT));
        return;
    }

    std::error_code ec;
    if (std::filesystem::is_directory(*TARGET, ec)) {
        navigate(*TARGET);
        return;
    }

    if (m_options.mode == PICKER_MODE_OPEN && std::filesystem::is_regular_file(*TARGET, ec)) {
        finish({*TARGET});
        return;
    }

    // a path to a new file, save it there
    if (m_options.mode == PICKER_MODE_SAVE && std::filesystem::is_directory(TARGET->parent_path(), ec)) {
        navigate(TARGET->parent_path());
        m_nameBox->setText(TARGET->filename().string());
        m_lastInput = INPUT_NAME;
        updateAccept();
        return;
    }

    setMessage(std::format("{} doesn't exist", TEXT));
}

void CFilePicker::accept() {
    const auto& ENTRIES   = m_list->entries();
    const auto  SELECTION = m_list->selection();

    switch (m_options.mode) {
        case PICKER_MODE_OPEN: {
            std::vector<std::filesystem::path>   files;
            std::optional<std::filesystem::path> dir;
            for (const auto INDEX : SELECTION) {
                if (ENTRIES[INDEX].directory)
                    dir = dir.value_or(m_dir / ENTRIES[INDEX].name);
                else
                    files.emplace_back(m_dir / ENTRIES[INDEX].name);
            }

            // opening a folder means going into it
            if (files.empty()) {
                if (dir)
                    navigate(*dir);
                return;
            }

            if (!m_options.multiple)
                files.resize(1);

            finish(std::move(files));
            return;
        }

        case PICKER_MODE_DIRECTORY: {
            std::vector<std::filesystem::path> dirs;
            for (const auto INDEX : SELECTION) {
                if (ENTRIES[INDEX].directory)
                    dirs.emplace_back(m_dir / ENTRIES[INDEX].name);
            }

            if (dirs.empty())
                dirs.emplace_back(m_dir);

            if (!m_options.multiple)
                dirs.resize(1);

            finish(std::move(dirs));
            return;
        }

        case PICKER_MODE_SAVE: {
            // saving with a folder picked in the list goes into that folder
            if (m_lastInput == INPUT_LIST && SELECTION.size() == 1 && ENTRIES[SELECTION.front()].directory) {
                navigate(m_dir / ENTRIES[SELECTION.front()].name);
                return;
            }

            const auto TARGET = resolve(m_nameBox->currentText());
            if (!TARGET)
                return;

            std::error_code ec;
            if (std::filesystem::is_directory(*TARGET, ec)) {
                navigate(*TARGET);
                m_nameBox->setText("");
                updateAccept();
                return;
            }

            if (!std::filesystem::is_directory(TARGET->parent_path(), ec)) {
                setMessage(std::format("The folder {} doesn't exist", TARGET->parent_path().string()));
                return;
            }

            if (std::filesystem::exists(*TARGET, ec) && m_confirmOverwrite != *TARGET) {
                m_confirmOverwrite = *TARGET;
                setMessage(std::format("{} already exists, saving will replace it", TARGET->filename().string()));
                updateAccept();
                return;
            }

            finish({*TARGET});
            return;
        }
    }
}

void CFilePicker::finish(std::vector<std::filesystem::path> paths) {
    m_result = SPickerResult{
        .paths   = std::move(paths),
        .filter  = m_options.filters.empty() ? std::nullopt : std::optional{m_filterIndex},
        .choices = m_choiceValues,
    };

    m_window->close();
    m_backend->destroy();
}

void CFilePicker::cancel() {
    m_result.reset();
    m_window->close();
    m_backend->destroy();
}

void CFilePicker::openNewFolder() {
    m_newFolderOpen = true;
    m_newFolderBox->setText("");
    showToolbar(true);
    m_backend->addIdle([this] { m_newFolderBox->focus(); });
}

void CFilePicker::createFolder() {
    const auto NAME = std::string{trim(m_newFolderBox->currentText())};
    if (NAME.empty())
        return;

    if (NAME.contains('/') || NAME == "." || NAME == "..") {
        setMessage(std::format("{} isn't a valid folder name", NAME));
        return;
    }

    std::error_code ec;
    const auto      PATH = m_dir / NAME;
    if (!std::filesystem::create_directory(PATH, ec)) {
        setMessage(ec ? std::format("Can't create {}: {}", NAME, ec.message()) : std::format("{} already exists", NAME));
        return;
    }

    closeNewFolder();
    navigate(PATH);
}

void CFilePicker::closeNewFolder() {
    m_newFolderOpen = false;
    showToolbar(false);
}

void CFilePicker::releaseTextFocus() {
    // a textbox only gives up focus it has, so ask every one of them
    for (const auto& textbox : {m_pathBox, m_nameBox, m_newFolderBox}) {
        if (textbox)
            textbox->focus(false);
    }
}

bool CFilePicker::textFocused() const {
    for (const auto& textbox : {m_pathBox, m_nameBox, m_newFolderBox}) {
        if (textbox && textbox->focused())
            return true;
    }

    return false;
}

void CFilePicker::setMessage(const std::string& message) {
    m_message->setText(escapeMarkup(message));
    m_quickSearchShown = false;
}

void CFilePicker::updateNavigation() {
    m_pathText = m_dir.string();
    m_pathBox->setText(m_pathText);

    m_backButton->setEnabled(m_historyPos > 0);
    m_forwardButton->setEnabled(m_historyPos + 1 < m_history.size());
    m_upButton->setEnabled(m_dir.has_relative_path());

    for (size_t i = 0; i < m_places.size(); ++i) {
        m_placeRows[i]->setState(m_places[i].path == m_dir ? ROW_STATE_SELECTED : ROW_STATE_NORMAL);
    }

    updateAccept();
}

void CFilePicker::updateAccept() {
    bool enabled = true;
    if (m_options.mode == PICKER_MODE_OPEN)
        enabled = !m_list->selection().empty();
    else if (m_options.mode == PICKER_MODE_SAVE)
        enabled = !trim(m_nameBox->currentText()).empty();

    m_acceptButton->setEnabled(enabled);

    const auto LABEL = m_confirmOverwrite ? std::string{"Replace"} : defaultAcceptLabel();
    if (LABEL == m_acceptLabelShown)
        return;

    m_acceptLabelShown = LABEL;
    m_acceptButton->setLabel(escapeMarkup(LABEL));
}

std::string CFilePicker::defaultAcceptLabel() const {
    if (!m_options.acceptLabel.empty())
        return stripMnemonics(m_options.acceptLabel);

    switch (m_options.mode) {
        case PICKER_MODE_SAVE: return "Save";
        case PICKER_MODE_DIRECTORY: return "Select";
        default: return "Open";
    }
}

std::string CFilePicker::windowTitle() const {
    if (!m_options.title.empty())
        return m_options.title;

    switch (m_options.mode) {
        case PICKER_MODE_SAVE: return "Save File";
        case PICKER_MODE_DIRECTORY: return m_options.multiple ? "Select Folders" : "Select Folder";
        default: return m_options.multiple ? "Open Files" : "Open File";
    }
}

std::filesystem::path CFilePicker::initialDirectory() const {
    std::error_code ec;
    if (!m_options.folder.empty() && std::filesystem::is_directory(m_options.folder, ec))
        return std::filesystem::absolute(m_options.folder, ec);

    if (const auto HOME = std::getenv("HOME"); HOME && *HOME && std::filesystem::is_directory(HOME, ec))
        return HOME;

    return "/";
}

std::optional<std::filesystem::path> CFilePicker::resolve(std::string_view text) const {
    const auto TRIMMED = trim(text);
    if (TRIMMED.empty())
        return std::nullopt;

    const auto RESOLVED = Hyprutils::Path::resolvePath(std::string{TRIMMED}, m_dir.string());
    if (!RESOLVED)
        return std::nullopt;

    return std::filesystem::path{*RESOLVED}.lexically_normal();
}
