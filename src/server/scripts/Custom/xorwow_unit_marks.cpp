/*
 * XorWoW: what the XorWoW addon needs for its unit marks - a small icon on name plates and in unit
 * tooltips for bots, alts played by the bot AI ("altbots") and creatures that drop an XorWoW TCG card.
 * They replace the old " *" / " @" name suffixes; the client cannot tell a bot from a player by itself.
 *
 * Protocol: addon messages, prefix "XorWoW", whispered by the addon to its own player (swallowed here),
 * answered the same way:
 *   "MARK;Q;Name,Name,..."  which of these online characters are bots
 *       -> "MARK;R;Name=*,Name=@,Name"   '*' bot, '@' altbot, no mark: a player, offline or unknown
 *          (the names exactly as asked; split over several messages when long)
 *   "MARK;C"                the names of the creatures that drop a card
 *       -> "MARK;C;Name,Name,..."         as many messages as it takes
 * Name plates only show a name, so creatures are matched by name: every creature whose loot table holds
 * a card (items 261001-261999), read once at startup.
 */

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <string>
#include <vector>

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr size_t MAX_BODY = 230;      // an addon message is 255 bytes with the prefix
    constexpr size_t MAX_ASKED = 40;      // names per question; the addon asks for fewer
    constexpr size_t MAX_NAME = 48;       // bytes; character names are 12 letters

    std::vector<std::string> cardCreatures;

    void Send(Player* player, std::string const& body)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\tMARK;{}", ADDON_PREFIX, body));
        player->SendDirectMessage(&data);
    }

    // Sends "<head>;item,item,..." in as many messages as the items need.
    void SendList(Player* player, std::string const& head, std::vector<std::string> const& items)
    {
        std::string body;
        for (std::string const& item : items)
        {
            if (!body.empty() && body.size() + 1 + item.size() > MAX_BODY)
            {
                Send(player, head + ";" + body);
                body.clear();
            }
            if (!body.empty())
                body += ',';
            body += item;
        }
        if (!body.empty())
            Send(player, head + ";" + body);
    }

    void Answer(Player* player, std::string const& names)
    {
        std::vector<std::string> answers;
        size_t start = 0;
        while (start <= names.size() && answers.size() < MAX_ASKED)
        {
            size_t end = names.find(',', start);
            if (end == std::string::npos)
                end = names.size();
            std::string asked = names.substr(start, end - start);
            start = end + 1;
            if (asked.empty() || asked.size() > MAX_NAME || asked.find_first_of(";=") != std::string::npos)
                continue;

            std::string name = asked;
            char mark = 0;
            if (normalizePlayerName(name))
                if (Player* target = ObjectAccessor::FindPlayerByName(name, false))
                    mark = target->GetSession()->GetBotNameMark();
            answers.push_back(mark ? asked + '=' + mark : asked);
        }
        SendList(player, "R", answers);
    }
}

class xorwow_unit_marks_playerscript : public PlayerScript
{
public:
    xorwow_unit_marks_playerscript() : PlayerScript("xorwow_unit_marks_playerscript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tMARK;", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        if (!player->GetSession()->IsBot())
        {
            std::string const body = msg.substr(prefix.size());
            if (body == "C")
                SendList(player, "C", cardCreatures);
            else if (body.rfind("Q;", 0) == 0)
                Answer(player, body.substr(2));
        }
        return false;
    }
};

class xorwow_unit_marks_worldscript : public WorldScript
{
public:
    xorwow_unit_marks_worldscript() : WorldScript("xorwow_unit_marks_worldscript", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        cardCreatures.clear();
        if (QueryResult result = WorldDatabase.Query(
            "SELECT DISTINCT ct.`name` FROM `creature_template` ct JOIN `creature_loot_template` l ON l.`Entry` = ct.`lootid` "
            "WHERE ct.`lootid` <> 0 AND l.`Item` BETWEEN 261001 AND 261999"))
        {
            do
            {
                std::string name = result->Fetch()[0].Get<std::string>();
                if (!name.empty() && name.size() <= MAX_NAME && name.find_first_of(",;") == std::string::npos)
                    cardCreatures.push_back(std::move(name));
            } while (result->NextRow());
        }
        LOG_INFO("server.loading", ">> XorWoW unit marks: {} creature names drop an XorWoW TCG card", cardCreatures.size());
    }
};

void AddSC_xorwow_unit_marks()
{
    new xorwow_unit_marks_playerscript();
    new xorwow_unit_marks_worldscript();
}
