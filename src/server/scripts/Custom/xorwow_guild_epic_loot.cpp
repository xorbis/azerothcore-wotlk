/*
 * XorWoW: guild chat announces a guild member's epic loot.
 *
 * When a guild member loots an epic (or legendary) item that can be worn - armor, weapons, rings,
 * trinkets, relics; not bags, quivers or ammo - the whole guild sees, in guild chat:
 *
 *     [Name] has looted [Item]!
 *
 * It is sent as CHAT_MSG_GUILD_ACHIEVEMENT, the line the client prints for "has earned the
 * achievement": guild colour, clickable name and item link, and nobody appears to be talking, so
 * bots do not treat it as a guild chat command. The looter sees it too.
 *
 * Covered: looting a corpse/chest yourself (OnPlayerLootItem), winning a need/greed roll
 * (OnPlayerGroupRollRewardItem) and the master looter handing it to you (LootHandler calls
 * OnPlayerLootItem for that too). Not covered: a roll won while the bags are full (the item comes
 * by mail), vendors, crafting, quest rewards, mail and trades.
 */

#include "DBCStores.h"
#include "Chat.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"

namespace
{
    bool IsWearable(ItemTemplate const* proto)
    {
        switch (proto->InventoryType)
        {
            case INVTYPE_NON_EQUIP:
            case INVTYPE_BAG:
            case INVTYPE_QUIVER:
            case INVTYPE_AMMO:
                return false;
            default:
                return true;
        }
    }

    // The same link the client builds for the item, random "of the Bear" suffix included.
    std::string ItemLink(Item const* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        std::string name = proto->Name1;
        int32 const randomId = item->GetItemRandomPropertyId();
        uint32 uniqueId = 0;
        if (randomId > 0)
        {
            if (ItemRandomPropertiesEntry const* prop = sItemRandomPropertiesStore.LookupEntry(randomId))
                if (prop->Name[0] && *prop->Name[0])
                    name += Acore::StringFormat(" {}", prop->Name[0]);
        }
        else if (randomId < 0)
        {
            if (ItemRandomSuffixEntry const* suffix = sItemRandomSuffixStore.LookupEntry(-randomId))
                if (suffix->Name[0] && *suffix->Name[0])
                    name += Acore::StringFormat(" {}", suffix->Name[0]);
            uniqueId = item->GetItemSuffixFactor();
        }

        return Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:{}:{}:0|h[{}]|h|r",
            ItemQualityColors[proto->Quality], proto->ItemId, randomId, uniqueId, name);
    }

    void AnnounceEpicLoot(Player* player, Item* item)
    {
        if (!player || !item || !player->GetGuildId())
            return;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || (proto->Quality != ITEM_QUALITY_EPIC && proto->Quality != ITEM_QUALITY_LEGENDARY) || !IsWearable(proto))
            return;

        Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
        if (!guild)
            return;

        // The client formats the text with the sender's name link in place of %s.
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_GUILD_ACHIEVEMENT, LANG_UNIVERSAL, player, player,
            Acore::StringFormat("%s has looted {}!", ItemLink(item)));
        guild->BroadcastPacket(&data);
    }
}

class xorwow_guild_epic_loot_playerscript : public PlayerScript
{
public:
    xorwow_guild_epic_loot_playerscript() : PlayerScript("xorwow_guild_epic_loot_playerscript",
        { PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM }) { }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        AnnounceEpicLoot(player, item);
    }

    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 /*count*/, RollVote /*voteType*/, Roll* /*roll*/) override
    {
        AnnounceEpicLoot(player, item);
    }
};

void AddSC_xorwow_guild_epic_loot()
{
    new xorwow_guild_epic_loot_playerscript();
}
