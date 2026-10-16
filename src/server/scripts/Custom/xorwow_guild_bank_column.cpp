/*
 * XorWoW: the guild bank column a deposit fills first. The XorWoW addon puts a radio button over each
 * of the guild bank's seven columns; an item right-clicked from the bags (a deposit without a slot)
 * goes into the selected column first, then the columns to its right, then round to the leftmost.
 * Stacks are topped up in the same order before an empty slot is taken. The order itself is in
 * Guild::BankMoveItemData::CanStoreItemInTab, which asks Guild::XorWoWBankStartColumn.
 *
 * Protocol: addon message, prefix "XorWoW", whispered by the addon to its own player (swallowed here):
 *   "GBCOL;n"     n = 1-7, the selected column; sent when the bank opens and when the choice changes
 * Not saved: the addon keeps the choice and sends it again after each login. Bots never send it, so
 * they keep the stock order (column 1 first).
 */

#include "DataMap.h"
#include "Guild.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <string>

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr char COLUMN_KEY[] = "xorwow_guild_bank_column";

    // Kept on the player: its sessions run in the map threads, so no shared state.
    struct BankColumn : DataMap::Base
    {
        uint8 column = 0;    // 0-6
    };

    uint8 StartColumn(Player* player)
    {
        BankColumn const* data = player ? player->CustomData.Get<BankColumn>(COLUMN_KEY) : nullptr;
        return data ? data->column : 0;
    }
}

class xorwow_guild_bank_column_playerscript : public PlayerScript
{
public:
    xorwow_guild_bank_column_playerscript() : PlayerScript("xorwow_guild_bank_column_playerscript",
        { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tGBCOL;", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        std::string const body = msg.substr(prefix.size());
        if (body.size() == 1 && body[0] >= '1' && body[0] <= '7')
            player->CustomData.GetDefault<BankColumn>(COLUMN_KEY)->column = uint8(body[0] - '1');
        return false;
    }
};

void AddSC_xorwow_guild_bank_column()
{
    new xorwow_guild_bank_column_playerscript();
    Guild::XorWoWBankStartColumn = StartColumn;
}
