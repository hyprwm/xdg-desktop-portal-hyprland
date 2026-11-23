#include "FileChooser.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../shared/Session.hpp"

#include <hyprutils/eventLoop/EventLoop.hpp>
#include <hyprutils/os/FileDescriptor.hpp>
#include <hyprutils/os/Process.hpp>

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace Hyprutils::EventLoop;

// a picker printing more than this is broken
static constexpr size_t MAX_OUTPUT = 4 * 1024 * 1024;

using FilterStruct = sdbus::Struct<std::string, std::vector<sdbus::Struct<uint32_t, std::string>>>;
using ChoiceStruct = sdbus::Struct<std::string, std::string, std::vector<sdbus::Struct<std::string, std::string>>, std::string>;

static SFileChooserFilter fromDBus(const FilterStruct& raw) {
    SFileChooserFilter filter{.name = raw.get<0>()};
    for (const auto& rule : raw.get<1>()) {
        filter.rules.emplace_back(rule.get<0>(), rule.get<1>());
    }

    return filter;
}

static FilterStruct toDBus(const SFileChooserFilter& filter) {
    std::vector<sdbus::Struct<uint32_t, std::string>> rules;
    for (const auto& [type, pattern] : filter.rules) {
        rules.emplace_back(type, pattern);
    }

    return FilterStruct{filter.name, rules};
}

// byte strings come NUL terminated
static std::string fromBytes(const std::vector<uint8_t>& bytes) {
    std::string text{bytes.begin(), bytes.end()};
    if (const auto NUL = text.find('\0'); NUL != std::string::npos)
        text.resize(NUL);

    return text;
}

// throws sdbus::Error on options of the wrong type
static SFileChooserRequest parseRequest(eFileChooserMethod method, std::string title, const std::unordered_map<std::string, sdbus::Variant>& options) {
    SFileChooserRequest request{.method = method, .title = std::move(title)};

    for (const auto& [key, value] : options) {
        if (key == "accept_label")
            request.acceptLabel = value.get<std::string>();
        else if (key == "multiple")
            request.multiple = value.get<bool>();
        else if (key == "directory")
            request.directory = value.get<bool>();
        else if (key == "current_name")
            request.currentName = value.get<std::string>();
        else if (key == "current_folder")
            request.currentFolder = fromBytes(value.get<std::vector<uint8_t>>());
        else if (key == "current_file")
            request.currentFile = fromBytes(value.get<std::vector<uint8_t>>());
        else if (key == "current_filter")
            request.currentFilter = fromDBus(value.get<FilterStruct>());
        else if (key == "filters") {
            for (const auto& filter : value.get<std::vector<FilterStruct>>()) {
                request.filters.emplace_back(fromDBus(filter));
            }
        } else if (key == "choices") {
            for (const auto& raw : value.get<std::vector<ChoiceStruct>>()) {
                SFileChooserChoice choice{.id = raw.get<0>(), .label = raw.get<1>(), .initial = raw.get<3>()};
                for (const auto& option : raw.get<2>()) {
                    choice.options.emplace_back(option.get<0>(), option.get<1>());
                }

                request.choices.emplace_back(std::move(choice));
            }
        } else if (key == "files") {
            for (const auto& file : value.get<std::vector<std::vector<uint8_t>>>()) {
                request.files.emplace_back(fromBytes(file));
            }
        }
    }

    return request;
}

CFileChooserPortal::CFileChooserPortal() {
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    const auto HANDLER = [this](eFileChooserMethod method) {
        return [this, method](Result&& result, sdbus::ObjectPath handle, std::string appID, std::string parentWindow, std::string title,
                              std::unordered_map<std::string, sdbus::Variant> options) { onCall(method, std::move(result), handle, appID, std::move(title), options); };
    };

    m_pObject
        ->addVTable(sdbus::registerMethod("OpenFile").implementedAs(HANDLER(FILE_CHOOSER_OPEN_FILE)),
                    sdbus::registerMethod("SaveFile").implementedAs(HANDLER(FILE_CHOOSER_SAVE_FILE)),
                    sdbus::registerMethod("SaveFiles").implementedAs(HANDLER(FILE_CHOOSER_SAVE_FILES)))
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[filechooser] init successful");
}

CFileChooserPortal::~CFileChooserPortal() {
    for (const auto& pending : m_vRequests) {
        if (!pending->done)
            stopPicker(pending);
    }
}

std::string CFileChooserPortal::pickerBinary() {
    static auto* const* PCUSTOMPICKER = rc<Hyprlang::STRING const*>(g_pPortalManager->m_sConfig.config->getConfigValuePtr("filechooser:custom_picker_binary")->getDataStaticPtr());

    const std::string   CUSTOM = *PCUSTOMPICKER;
    return CUSTOM.empty() ? "hyprland-file-picker" : CUSTOM;
}

void CFileChooserPortal::onCall(eFileChooserMethod method, Result&& result, const sdbus::ObjectPath& handle, const std::string& appID, std::string title,
                                const std::unordered_map<std::string, sdbus::Variant>& options) {
    Debug::log(LOG, "[filechooser] New request:");
    Debug::log(LOG, "[filechooser]  | {}", handle.c_str());
    Debug::log(LOG, "[filechooser]  | appid: {}", appID);

    auto pending    = makeShared<SPendingRequest>();
    pending->result = std::move(result);

    try {
        pending->request = parseRequest(method, std::move(title), options);
    } catch (const sdbus::Error& e) {
        Debug::log(ERR, "[filechooser] Rejecting malformed options: {}", e.what());
        pending->result.returnResults(2, {});
        return;
    }

    int fds[2] = {-1, -1};
    if (pipe2(fds, O_CLOEXEC) != 0) {
        Debug::log(ERR, "[filechooser] Couldn't create a pipe: {}", strerror(errno));
        pending->result.returnResults(2, {});
        return;
    }

    Hyprutils::OS::CFileDescriptor readEnd{fds[0]};
    Hyprutils::OS::CFileDescriptor writeEnd{fds[1]};

    // only our end is non-blocking, the picker writes its stdout as usual
    fcntl(readEnd.get(), F_SETFL, fcntl(readEnd.get(), F_GETFL) | O_NONBLOCK);

    const auto              BINARY = pickerBinary();
    Hyprutils::OS::CProcess process(BINARY, buildFilePickerArgs(pending->request));
    process.setStdoutFD(writeEnd.get());
    if (!process.runAsync()) {
        Debug::log(ERR, "[filechooser] Couldn't run {}", BINARY);
        pending->result.returnResults(2, {});
        return;
    }

    // the picker holds the only write end now, so its exit reads as EOF
    writeEnd.reset();
    pending->pid = process.pid();

    const WP<SPendingRequest> WEAK = pending;
    auto source = g_pPortalManager->eventLoop()->addFD(std::move(readEnd), eEventMask::READABLE, [this, WEAK](IFDSource&, FdEventMask events) { onOutput(WEAK, events); });
    if (!source) {
        Debug::log(ERR, "[filechooser] Couldn't watch the picker: {}", source.error());
        kill(pending->pid, SIGTERM);
        pending->result.returnResults(2, {});
        return;
    }

    pending->output = *source;

    pending->dbusRequest            = createDBusRequest(handle);
    pending->dbusRequest->onDestroy = [this, WEAK] { onClose(WEAK); };

    m_vRequests.emplace_back(std::move(pending));
}

void CFileChooserPortal::onOutput(const WP<SPendingRequest>& weak, FdEventMask events) {
    const auto PENDING = weak.lock();
    if (!PENDING || PENDING->done)
        return;

    if (events & eEventMask::READABLE) {
        std::array<char, 4096> buffer;
        const auto             LEN = read(PENDING->output->fd().get(), buffer.data(), buffer.size());

        if (LEN < 0 && (errno == EAGAIN || errno == EINTR))
            return;

        if (LEN > 0) {
            PENDING->stdOut.append(buffer.data(), LEN);
            if (PENDING->stdOut.size() <= MAX_OUTPUT)
                return;

            Debug::log(ERR, "[filechooser] The picker printed too much, giving up");
            stopPicker(PENDING);
            complete(PENDING, 2, {});
            return;
        }

        // EOF, or a read error: either way the picker is done
    } else if (!(events & (eEventMask::HUP | eEventMask::ERROR)))
        return;

    respond(PENDING);
}

void CFileChooserPortal::respond(const SP<SPendingRequest>& pending) {
    const auto RESPONSE = parseFilePickerOutput(pending->stdOut, pending->request, [](const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    });

    if (!RESPONSE) {
        Debug::log(LOG, "[filechooser] Nothing was picked");
        complete(pending, 1, {});
        return;
    }

    std::unordered_map<std::string, sdbus::Variant> results;
    results["uris"] = sdbus::Variant{RESPONSE->uris};

    if (!RESPONSE->choices.empty()) {
        std::vector<sdbus::Struct<std::string, std::string>> choices;
        for (const auto& [id, value] : RESPONSE->choices) {
            choices.emplace_back(id, value);
        }

        results["choices"] = sdbus::Variant{choices};
    }

    if (RESPONSE->currentFilter)
        results["current_filter"] = sdbus::Variant{toDBus(*RESPONSE->currentFilter)};

    // there's no read-only mode, the app gets what it asked for
    if (pending->request.method == FILE_CHOOSER_OPEN_FILE)
        results["writable"] = sdbus::Variant{true};

    Debug::log(LOG, "[filechooser] Picked {} uri(s)", RESPONSE->uris.size());
    complete(pending, 0, std::move(results));
}

void CFileChooserPortal::onClose(const WP<SPendingRequest>& weak) {
    const auto PENDING = weak.lock();
    if (!PENDING || PENDING->done)
        return;

    Debug::log(LOG, "[filechooser] Request closed, dismissing the picker");
    stopPicker(PENDING);
    complete(PENDING, 2, {});
}

void CFileChooserPortal::stopPicker(const SP<SPendingRequest>& pending) {
    if (pending->pid <= 0 || !pending->output)
        return;

    // once the picker exits its pid may get reused, it's only ours while it holds the pipe
    pollfd fd = {.fd = pending->output->fd().get(), .events = POLLIN, .revents = 0};
    if (poll(&fd, 1, 0) > 0 && (fd.revents & POLLHUP))
        return;

    kill(pending->pid, SIGTERM);
}

void CFileChooserPortal::complete(const SP<SPendingRequest>& pending, uint32_t response, std::unordered_map<std::string, sdbus::Variant> results) {
    if (pending->done)
        return;

    pending->done = true;

    try {
        pending->result.returnResults(response, results);
    } catch (const sdbus::Error& e) { Debug::log(ERR, "[filechooser] Couldn't reply: {}", e.what()); }

    if (pending->output)
        pending->output->remove();

    // this runs inside the output callback or the request's Close handler, clean up after them
    g_pPortalManager->addIdle([this, pending] { std::erase(m_vRequests, pending); });
}
