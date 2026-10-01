/*
 * XorWoW: the song title for the XorWoW addon's radio button.
 *
 * The radio itself (Everlook Broadcasting Co., Turtle WoW's community station) plays in the XorWoW
 * launcher on the player's PC - the client cannot play a stream - and neither the launcher nor the
 * addon can tell the game what is on air. The station's Icecast server publishes it
 * (https://radio.turtle-music.org/status-json.xsl, icestats.source.title), so this script reads it
 * every 20 seconds and tells each listening player's addon at every new song:
 * "XorWoW\tRADIO;<title>" (an addon whisper to the player).
 *
 * The addon whispers "XorWoW\tRADIO;on" / "RADIO;off" to itself when the radio starts or stops
 * (swallowed here); the server forgets it at logout, so the addon sends it again at every login.
 * Nobody listening, nothing is fetched. The fetch runs on its own thread (HTTPS with Boost.Beast,
 * every step under a timeout), never on the world thread.
 */

#include "Chat.h"
#include "DataMap.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = boost::beast::http;
    using namespace std::chrono_literals;

    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr char LISTENER_KEY[] = "xorwow-radio";
    constexpr char STATUS_HOST[] = "radio.turtle-music.org";
    constexpr char STATUS_PATH[] = "/status-json.xsl";
    constexpr auto FETCH_EVERY = 20s;
    constexpr auto FETCH_TIMEOUT = 15s;
    constexpr auto LISTENED_WITHIN = 30s;   // the last listener's update this long ago: stop fetching
    constexpr uint32 CHECK_INTERVAL_MS = 1000;
    constexpr size_t MAX_TITLE = 200;   // an addon message is 255 bytes with the prefix

    struct Listener : public DataMap::Base
    {
        bool enabled = false;
        uint32 sentVersion = 0;   // the title version this player's addon has
        uint32 timer = 0;
    };

    // Shared between the world thread and the fetcher.
    std::mutex titleLock;
    std::string title;
    std::atomic<uint32> titleVersion{0};
    std::atomic<int64> listenedAt{0};   // steady clock, seconds

    int64 SteadySeconds()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void AppendUtf8(std::string& out, uint32 cp)
    {
        if (cp < 0x80)
            out += char(cp);
        else if (cp < 0x800)
        {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        else
        {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    // The first "title":"..." of the status JSON (one mount on that server), unescaped; empty if none.
    std::string ParseTitle(std::string const& json)
    {
        size_t pos = json.find("\"title\"");
        if (pos == std::string::npos)
            return "";
        pos = json.find_first_not_of(" \t\r\n:", pos + 7);
        if (pos == std::string::npos || json[pos] != '"')
            return "";   // null, or not a string

        std::string out;
        for (size_t i = pos + 1; i < json.size(); ++i)
        {
            char c = json[i];
            if (c == '"')
                return out;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (++i >= json.size())
                break;
            switch (json[i])
            {
                case 'n': case 'r': case 't': out += ' '; break;
                case 'b': case 'f': break;
                case 'u':
                {
                    if (i + 4 >= json.size())
                        return "";
                    uint32 cp = std::stoul(json.substr(i + 1, 4), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u')
                    {
                        uint32 low = std::stoul(json.substr(i + 3, 4), nullptr, 16);
                        if (low >= 0xDC00 && low < 0xE000)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            i += 6;
                        }
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: out += json[i]; break;   // \" \\ \/
            }
        }
        return "";   // unterminated
    }

    // Cut to MAX_TITLE bytes without splitting a UTF-8 character.
    std::string Clip(std::string text)
    {
        if (text.size() <= MAX_TITLE)
            return text;
        size_t cut = MAX_TITLE;
        while (cut > 0 && (uint8(text[cut]) & 0xC0) == 0x80)
            --cut;
        return text.substr(0, cut);
    }

    asio::awaitable<void> Get(asio::ssl::context& tls, std::string& body)
    {
        auto executor = co_await asio::this_coro::executor;
        asio::ip::tcp::resolver resolver(executor);
        beast::ssl_stream<beast::tcp_stream> stream(executor, tls);
        if (!SSL_set_tlsext_host_name(stream.native_handle(), STATUS_HOST))
            throw beast::system_error(beast::error_code(int(::ERR_get_error()), asio::error::get_ssl_category()));
        stream.set_verify_callback(asio::ssl::host_name_verification(STATUS_HOST));

        auto endpoints = co_await resolver.async_resolve(STATUS_HOST, "443", asio::use_awaitable);
        beast::get_lowest_layer(stream).expires_after(FETCH_TIMEOUT);
        co_await beast::get_lowest_layer(stream).async_connect(endpoints, asio::use_awaitable);
        co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);

        http::request<http::empty_body> request{ http::verb::get, STATUS_PATH, 11 };
        request.set(http::field::host, STATUS_HOST);
        request.set(http::field::user_agent, "XorWoW realm (song title for the in-game radio button)");
        co_await http::async_write(stream, request, asio::use_awaitable);

        beast::flat_buffer buffer;
        http::response<http::string_body> response;
        co_await http::async_read(stream, buffer, response, asio::use_awaitable);
        if (response.result() != http::status::ok)
            throw std::runtime_error(Acore::StringFormat("HTTP {}", response.result_int()));
        body = std::move(response.body());
        // no TLS shutdown: the server may not answer it, and the connection is dropped anyway
    }

    // One fetch, all of it within FETCH_TIMEOUT; throws on any failure.
    std::string Fetch()
    {
        asio::io_context io;
        asio::ssl::context tls(asio::ssl::context::tls_client);
        tls.set_default_verify_paths();
        tls.set_verify_mode(asio::ssl::verify_peer);

        std::string body;
        std::exception_ptr failure;
        bool done = false;
        asio::co_spawn(io, Get(tls, body), [&](std::exception_ptr e) { failure = e; done = true; });
        io.run_for(FETCH_TIMEOUT + 2s);
        if (!done)
            throw std::runtime_error("timed out");
        if (failure)
            std::rethrow_exception(failure);
        return ParseTitle(body);
    }

    class Fetcher
    {
    public:
        ~Fetcher()
        {
            Stop();   // a std::thread still joinable at exit would abort the process
        }

        void Start()
        {
            _thread = std::thread([this] { Run(); });
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> guard(_lock);
                _stop = true;
            }
            _wake.notify_all();
            if (_thread.joinable())
                _thread.join();
        }

    private:
        void Run()
        {
            bool failing = false;
            auto next = std::chrono::steady_clock::now();
            std::unique_lock<std::mutex> guard(_lock);
            while (!_stop)
            {
                _wake.wait_for(guard, 1s, [this] { return _stop; });
                if (_stop)
                    break;
                if (SteadySeconds() - listenedAt.load() > std::chrono::seconds(LISTENED_WITHIN).count())
                    continue;   // nobody listening
                if (std::chrono::steady_clock::now() < next)
                    continue;
                next = std::chrono::steady_clock::now() + FETCH_EVERY;

                guard.unlock();
                try
                {
                    std::string fresh = Clip(Fetch());
                    if (failing)
                        LOG_INFO("server.worldserver", "XorWoW radio: song titles are back");
                    failing = false;
                    std::lock_guard<std::mutex> titleGuard(titleLock);
                    if (!fresh.empty() && fresh != title)
                    {
                        title = fresh;
                        ++titleVersion;
                    }
                }
                catch (std::exception const& e)
                {
                    if (!failing)
                        LOG_WARN("server.worldserver", "XorWoW radio: cannot read the song title ({}), retrying every {} s",
                            e.what(), std::chrono::seconds(FETCH_EVERY).count());
                    failing = true;
                }
                guard.lock();
            }
        }

        std::thread _thread;
        std::mutex _lock;
        std::condition_variable _wake;
        bool _stop = false;
    };

    Fetcher fetcher;

    void SendToAddon(Player* player, std::string const& body)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\t{}", ADDON_PREFIX, body));
        player->SendDirectMessage(&data);
    }
}

class xorwow_radio_playerscript : public PlayerScript
{
public:
    xorwow_radio_playerscript() : PlayerScript("xorwow_radio_playerscript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_UPDATE }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tRADIO;", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        std::string state = msg.substr(prefix.size());
        if (state != "on" && state != "off")
            return true;

        Listener* listener = player->CustomData.GetDefault<Listener>(LISTENER_KEY);
        listener->enabled = state == "on";
        listener->sentVersion = 0;   // a fresh start hears the current song at once
        listener->timer = CHECK_INTERVAL_MS;
        if (listener->enabled)
            listenedAt = SteadySeconds();
        return false;
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        Listener* listener = player->CustomData.Get<Listener>(LISTENER_KEY);
        if (!listener || !listener->enabled)
            return;

        listener->timer += diff;
        if (listener->timer < CHECK_INTERVAL_MS)
            return;
        listener->timer = 0;
        listenedAt = SteadySeconds();

        uint32 version = titleVersion.load();
        if (version == listener->sentVersion || !player->IsInWorld())
            return;

        std::string current;
        {
            std::lock_guard<std::mutex> guard(titleLock);
            current = title;
        }
        listener->sentVersion = version;
        if (!current.empty())
            SendToAddon(player, "RADIO;" + current);
    }
};

class xorwow_radio_worldscript : public WorldScript
{
public:
    xorwow_radio_worldscript() : WorldScript("xorwow_radio_worldscript", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_SHUTDOWN }) { }

    void OnStartup() override
    {
        fetcher.Start();
    }

    void OnShutdown() override
    {
        fetcher.Stop();
    }
};

void AddSC_xorwow_radio()
{
    new xorwow_radio_playerscript();
    new xorwow_radio_worldscript();
}
