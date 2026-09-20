/*
 * XorWoW: ".guild buytab" - buy the guild's next bank tab at the realm's price.
 *
 * The realm charges less than stock for guild bank tabs (worldserver.conf, Guild.BankTabCost0-5),
 * but the stock client never asks the server what a tab costs: its bank window refuses to send
 * the purchase at all while the guild master holds less gold than the client's own price table
 * says ("You can't afford that." is raised client-side; AzerothCore's handler even notes "this is
 * checked by client"). This command is the same purchase through the chat-command channel, which
 * the client does not pre-check: same rules (member of the guild, guild master, standing at a
 * guild bank, paid from the guild master's own gold at the realm's price) and the same core code,
 * Guild::HandleBuyBankTab, which also tells every online member and refreshes the window. The
 * XorWoW client addon routes the bank window's Purchase confirmation to it, so nobody types it;
 * without the addon it is ".guild buytab" in chat.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Guild.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

using namespace Acore::ChatCommands;

namespace
{
    std::string MoneyToString(uint32 copper)
    {
        uint32 const gold   = copper / 10000;
        uint32 const silver = (copper % 10000) / 100;
        uint32 const cop    = copper % 100;
        std::string out;
        if (gold)             out += Acore::StringFormat("{} gold", gold);
        if (silver)           out += Acore::StringFormat("{}{} silver", out.empty() ? "" : " ", silver);
        if (cop || out.empty()) out += Acore::StringFormat("{}{} copper", out.empty() ? "" : " ", cop);
        return out;
    }

    // Guild keeps its tab count private; the table it writes every new tab to is the
    // next best thing (only used for the messages - HandleBuyBankTab enforces the rules).
    uint8 CountBankTabs(uint32 guildId)
    {
        if (QueryResult result = CharacterDatabase.Query("SELECT COUNT(*) FROM guild_bank_tab WHERE guildid = {}", guildId))
            return result->Fetch()[0].Get<uint8>();
        return 0;
    }
}

class xorwow_guild_bank_tab_commandscript : public CommandScript
{
public:
    xorwow_guild_bank_tab_commandscript() : CommandScript("xorwow_guild_bank_tab_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable guildCommandTable =
        {
            { "buytab", HandleGuildBuyTabCommand, SEC_PLAYER, Console::No }
        };
        static ChatCommandTable commandTable =
        {
            { "guild", guildCommandTable }   // merged into the core's ".guild" tree
        };
        return commandTable;
    }

    static bool HandleGuildBuyTabCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        Guild* guild = player->GetGuild();
        if (!guild)
        {
            handler->SendSysMessage("You are not in a guild.");
            return true;
        }
        if (guild->GetLeaderGUID() != player->GetGUID())
        {
            handler->SendSysMessage("Only the guild master can buy guild bank tabs.");
            return true;
        }
        if (!player->FindNearestGameObjectOfType(GAMEOBJECT_TYPE_GUILD_BANK, INTERACTION_DISTANCE))
        {
            handler->SendSysMessage("You must be standing at a guild bank.");
            return true;
        }

        uint8 const tabs = CountBankTabs(guild->GetId());
        if (tabs >= GUILD_BANK_MAX_TABS)
        {
            handler->PSendSysMessage("{} already has all {} guild bank tabs.", guild->GetName(), uint32(GUILD_BANK_MAX_TABS));
            return true;
        }

        uint32 const cost = sWorld->getIntConfig(static_cast<ServerConfigs>(CONFIG_GUILD_BANK_TAB_COST_0 + tabs));
        if (!player->HasEnoughMoney(cost))
        {
            handler->PSendSysMessage("Guild bank tab {} costs {} - you have {}.", uint32(tabs) + 1, MoneyToString(cost), MoneyToString(player->GetMoney()));
            return true;
        }

        uint32 const before = player->GetMoney();
        guild->HandleBuyBankTab(handler->GetSession(), tabs);   // the stock purchase: checks, charges, creates, broadcasts

        if (player->GetMoney() < before)
            handler->PSendSysMessage("Guild bank tab {} bought for {}.", uint32(tabs) + 1, MoneyToString(before - player->GetMoney()));
        else
            handler->SendSysMessage("The purchase did not go through - close and reopen the guild bank and try again.");
        return true;
    }
};

void AddSC_xorwow_guild_bank_tab()
{
    new xorwow_guild_bank_tab_commandscript();
}
