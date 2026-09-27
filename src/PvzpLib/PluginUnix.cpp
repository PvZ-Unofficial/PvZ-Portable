#if defined(__linux__) || defined(__APPLE__)
#include "Plugin.h"
#include "PluginLayout.h"
#include <SDL.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>

namespace PvzpPlugin
{
bool LoadUnixPath(const char* path);
namespace
{
struct Client
{
    int fd = -1;
    std::array<unsigned char, 32772> input{};
    std::size_t received = 0;
    std::array<unsigned char, 8> output{};
    std::size_t sent = 0;
    bool replied = false;
    void Close() { if (fd >= 0) close(fd); fd = -1; received = sent = 0; replied = false; }
    ~Client() { Close(); }
};

struct Endpoint
{
    int fd = -1;
    std::string path;
    std::array<Client, 8> clients;
    Endpoint(const std::string& directory, const char* name)
    {
        path = directory + "/" + name + "-" + std::to_string(getpid());
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (path.size() >= sizeof(address.sun_path)) return;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return;
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        fcntl(fd, F_SETFL, O_NONBLOCK);
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 8) != 0)
        { close(fd); fd = -1; return; }
        chmod(path.c_str(), 0600);
    }
    ~Endpoint() { if (fd >= 0) { close(fd); unlink(path.c_str()); } }
    void Accept()
    {
        for (auto& client : clients)
        {
            if (client.fd >= 0) continue;
            client.fd = accept(fd, nullptr, nullptr);
            if (client.fd < 0) break;
            fcntl(client.fd, F_SETFD, FD_CLOEXEC);
            fcntl(client.fd, F_SETFL, O_NONBLOCK);
#ifdef __APPLE__
            int value = 1;
            setsockopt(client.fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));
#endif
        }
    }
};

std::string RuntimeDirectory()
{
    const char* configured = std::getenv("XDG_RUNTIME_DIR");
    std::string directory = configured ? configured : "/tmp/rsvz-" + std::to_string(getuid());
    if (!configured) mkdir(directory.c_str(), 0700);
    return directory;
}

struct Control
{
    Endpoint load{RuntimeDirectory(), "pvzp-plugin-load"};
    Endpoint watch{RuntimeDirectory(), "pvzp-game-watch"};
};

std::uint32_t ReadWord(const unsigned char* p)
{ return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }

void Reply(Client& client, std::uint32_t value)
{
    client.output[0] = 4;
    for (int i = 0; i != 4; ++i) client.output[4 + i] = static_cast<unsigned char>(value >> (8 * i));
    client.replied = true;
}

void PollClient(Client& client, bool watch)
{
    if (client.fd < 0) return;
    if (watch && !client.replied) Reply(client, AbiVersion);
    while (!client.replied)
    {
        std::size_t end = 4;
        if (client.received >= 4)
        {
            const auto size = ReadWord(client.input.data());
            if (!size || size > client.input.size() - 5) { client.Close(); return; }
            end += size;
        }
        if (client.received == end)
        {
            client.input[end] = 0;
            const char* path = reinterpret_cast<char*>(client.input.data() + 4);
            if (end == 5 && path[0] == 3)
            {
                Reply(client, gLawnApp->mPlugin.module || gLawnApp->mPlugin.enabled ? 1 : 0);
            }
            else if ((end == 5 && path[0] == 1) || (end == 6 && path[0] == 2))
            {
                auto* window = static_cast<SDL_Window*>(gLawnApp->mWindow);
                const char* driver = SDL_GetCurrentVideoDriver();
                const bool supported = window && driver && (std::strcmp(driver, "x11") == 0 || std::strcmp(driver, "cocoa") == 0);
                if (supported && path[0] == 2) SDL_SetWindowAlwaysOnTop(window, path[1] ? SDL_TRUE : SDL_FALSE);
                Reply(client, (supported ? 1u : 0u) | (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_ALWAYS_ON_TOP) ? 2u : 0u));
            }
            else
            {
                const bool valid = path[0] == '/' && std::strlen(path) == end - 4;
                Reply(client, valid && LoadUnixPath(path) ? 0 : 3);
            }
            break;
        }
        auto count = recv(client.fd, client.input.data() + client.received, end - client.received, 0);
        if (count > 0) client.received += count;
        else { if (!count || (errno != EAGAIN && errno != EWOULDBLOCK)) client.Close(); return; }
    }
    while (client.sent < client.output.size())
    {
#ifdef __linux__
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        auto count = send(client.fd, client.output.data() + client.sent, client.output.size() - client.sent, flags);
        if (count > 0) client.sent += count;
        else { if (!count || (errno != EAGAIN && errno != EWOULDBLOCK)) client.Close(); return; }
    }
    // The dedicated watch connection stays open independently of plugin lifetime.
    unsigned char byte;
    auto count = recv(client.fd, &byte, 1, MSG_PEEK);
    if (!count || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) client.Close();
}
}

void PollUnixControl()
{
    auto& host = gLawnApp->mPlugin;
    if (!host.unixControl) host.unixControl = new Control;
    auto& control = *static_cast<Control*>(host.unixControl);
    control.load.Accept();
    control.watch.Accept();
    for (auto& client : control.load.clients) PollClient(client, false);
    for (auto& client : control.watch.clients) PollClient(client, true);
}

void CloseUnixControl()
{
    delete static_cast<Control*>(gLawnApp->mPlugin.unixControl);
    gLawnApp->mPlugin.unixControl = nullptr;
}
}
#endif
