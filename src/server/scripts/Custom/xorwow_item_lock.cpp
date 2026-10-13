/*
 * XorWoW: item locks. A player locks an item in the bags or the bank (Ctrl-right-click in the XorWoW
 * addon) and it cannot leave the character until it is unlocked: no vendor sale or refund, trade,
 * mail, auction, guild bank deposit, destroy, disenchant, prospecting, milling or socketing (gems).
 * The sale is refused through the OnPlayerCanSellItem hook, the rest through Player::XorWoWItemLocked,
 * which the core asks at each of those places (and mod-playerbots before an altbot destroys an item).
 * So it holds for sell-junk addons and the altbot's sell and destroy rules too. Moving the item
 * between the bags, the bank and the paperdoll is fine, and so is using it up (a potion, a reagent,
 * a quest item handed in). The lock follows the item (its guid) wherever it moves.
 *
 * Protocol: addon messages, prefix "XorWoW", whispered by the addon to its own player (swallowed here),
 * answered the same way. Positions are the client's: bag 0-4 and 5-11, -1 for the bank's own slots,
 * slots from 1.
 *   "LOCK;Q"                 the locked items' positions
 *   "LOCK;T;bag;slot"        locks or unlocks the item there, then answers as for Q
 *       -> "LOCK;L;bag:slot,bag:slot,..."   the first (or only) message, possibly empty
 *          "LOCK;M;bag:slot,..."            more of the list, when it does not fit in one message
 * The addon asks again after its bags change, since only the server knows which item is where.
 *
 * Locks are saved in xorwow_item_lock (characters). Rows for items the character no longer has (used
 * up) are dropped at its next login.
 */

#include "Bag.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DataMap.h"
#include "Item.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <ctime>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr size_t MAX_BODY = 230;      // an addon message is 255 bytes with the prefix
    constexpr char LOCKS_KEY[] = "xorwow_item_lock";

    // Kept on the player: its sessions run in the map threads, so no shared state.
    struct Locks : DataMap::Base
    {
        std::unordered_set<ObjectGuid::LowType> items;
        time_t lastNotice = 0;     // the explanation goes out once a second at most: a cast is checked twice
    };

    Locks* GetLocks(Player* player)
    {
        return player->CustomData.GetDefault<Locks>(LOCKS_KEY);
    }

    // Is the item locked? notify: tell its owner how to unlock it (never a bot).
    bool IsLocked(Player* player, Item const* item, bool notify)
    {
        Locks* locks = player->CustomData.Get<Locks>(LOCKS_KEY);
        if (!locks || !locks->items.count(item->GetGUID().GetCounter()))
            return false;
        time_t const now = time(nullptr);
        if (notify && !player->GetSession()->IsBot() && locks->lastNotice != now)
        {
            locks->lastNotice = now;
            ChatHandler(player->GetSession()).PSendSysMessage("{} is locked. Ctrl-right-click it in your bags to unlock it.",
                item->GetTemplate()->Name1);
        }
        return true;
    }

    void Send(Player* player, std::string const& body)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\tLOCK;{}", ADDON_PREFIX, body));
        player->SendDirectMessage(&data);
    }

    // Client bag and slot -> the server's position, false for anything but the bags and the bank.
    bool ServerPos(int32 bag, int32 slot, uint8& serverBag, uint8& serverSlot)
    {
        if (slot < 1 || slot > MAX_BAG_SIZE)
            return false;
        if (bag == 0 || bag == -1)
        {
            uint8 const start = bag == 0 ? uint8(INVENTORY_SLOT_ITEM_START) : uint8(BANK_SLOT_ITEM_START);
            uint8 const end = bag == 0 ? uint8(INVENTORY_SLOT_ITEM_END) : uint8(BANK_SLOT_ITEM_END);
            if (start + slot - 1 >= end)
                return false;
            serverBag = INVENTORY_SLOT_BAG_0;
            serverSlot = start + slot - 1;
            return true;
        }
        if (bag >= 1 && bag <= 4)
            serverBag = INVENTORY_SLOT_BAG_START + bag - 1;
        else if (bag >= 5 && bag <= 11)
            serverBag = BANK_SLOT_BAG_START + bag - 5;
        else
            return false;
        serverSlot = slot - 1;
        return true;
    }

    void AddIfLocked(Locks const* locks, Item* item, int32 bag, int32 slot, std::vector<std::string>& out)
    {
        if (item && locks->items.count(item->GetGUID().GetCounter()))
            out.push_back(Acore::StringFormat("{}:{}", bag, slot));
    }

    void SendList(Player* player)
    {
        Locks const* locks = GetLocks(player);
        std::vector<std::string> positions;
        if (!locks->items.empty())
        {
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                AddIfLocked(locks, player->GetItemByPos(INVENTORY_SLOT_BAG_0, i), 0, i - INVENTORY_SLOT_ITEM_START + 1, positions);
            for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
                AddIfLocked(locks, player->GetItemByPos(INVENTORY_SLOT_BAG_0, i), -1, i - BANK_SLOT_ITEM_START + 1, positions);

            auto bags = [&](uint8 start, uint8 end, int32 firstClientBag)
            {
                for (uint8 b = start; b < end; ++b)
                    if (Bag* container = player->GetBagByPos(b))
                        for (uint32 s = 0; s < container->GetBagSize(); ++s)
                            AddIfLocked(locks, container->GetItemByPos(s), firstClientBag + (b - start), s + 1, positions);
            };
            bags(INVENTORY_SLOT_BAG_START, INVENTORY_SLOT_BAG_END, 1);
            bags(BANK_SLOT_BAG_START, BANK_SLOT_BAG_END, 5);
        }

        std::string head = "L;", body;
        for (std::string const& position : positions)
        {
            if (!body.empty() && body.size() + 1 + position.size() > MAX_BODY)
            {
                Send(player, head + body);
                head = "M;";
                body.clear();
            }
            if (!body.empty())
                body += ',';
            body += position;
        }
        Send(player, head + body);
    }

    void Toggle(Player* player, std::string const& args)
    {
        int32 bag = 0, slot = 0;
        size_t const split = args.find(';');
        if (split == std::string::npos)
            return;
        try
        {
            bag = std::stoi(args.substr(0, split));
            slot = std::stoi(args.substr(split + 1));
        }
        catch (...)
        {
            return;
        }

        uint8 serverBag = 0, serverSlot = 0;
        if (!ServerPos(bag, slot, serverBag, serverSlot))
            return;
        Item* item = player->GetItemByPos(serverBag, serverSlot);
        if (!item || item->GetOwnerGUID() != player->GetGUID())
            return;

        ObjectGuid::LowType const guid = item->GetGUID().GetCounter();
        Locks* locks = GetLocks(player);
        if (locks->items.erase(guid))
            CharacterDatabase.Execute("DELETE FROM `xorwow_item_lock` WHERE `item` = {}", guid);
        else
        {
            locks->items.insert(guid);
            CharacterDatabase.Execute("REPLACE INTO `xorwow_item_lock` (`owner`, `item`) VALUES ({}, {})", player->GetGUID().GetCounter(), guid);
        }
    }
}

class xorwow_item_lock_playerscript : public PlayerScript
{
public:
    xorwow_item_lock_playerscript() : PlayerScript("xorwow_item_lock_playerscript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_CAN_SELL_ITEM, PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    void OnPlayerLogin(Player* player) override
    {
        ObjectGuid::LowType const owner = player->GetGUID().GetCounter();
        Locks* locks = GetLocks(player);
        locks->items.clear();
        if (QueryResult result = CharacterDatabase.Query("SELECT l.`item` FROM `xorwow_item_lock` l "
            "JOIN `item_instance` i ON i.`guid` = l.`item` AND i.`owner_guid` = l.`owner` WHERE l.`owner` = {}", owner))
        {
            do
                locks->items.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());
        }
        CharacterDatabase.Execute("DELETE l FROM `xorwow_item_lock` l LEFT JOIN `item_instance` i "
            "ON i.`guid` = l.`item` AND i.`owner_guid` = l.`owner` WHERE l.`owner` = {} AND i.`guid` IS NULL", owner);
    }

    bool OnPlayerCanSellItem(Player* player, Item* item, Creature* /*creature*/) override
    {
        return !IsLocked(player, item, true);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tLOCK;", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        if (!player->GetSession()->IsBot())
        {
            std::string const body = msg.substr(prefix.size());
            if (body.rfind("T;", 0) == 0)
                Toggle(player, body.substr(2));
            if (body == "Q" || body.rfind("T;", 0) == 0)
                SendList(player);
        }
        return false;
    }
};

void AddSC_xorwow_item_lock()
{
    new xorwow_item_lock_playerscript();
    Player::XorWoWItemLocked = IsLocked;
}
