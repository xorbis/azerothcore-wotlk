/*
 * XorWoW: a sound when a herb or a mining node appears on the minimap.
 *
 * The client draws tracked herbs and ores on the minimap by itself: its Lua API has no list of
 * those blips and no event when one appears, so the XorWoW client addon cannot notice them alone.
 * A blip appears when two things are true: the node is in the objects the server has sent to the
 * client (its visibility range), and the player's tracking (Find Herbs / Find Minerals, the
 * PLAYER_TRACK_RESOURCES bits) matches the node's lock. This script checks exactly that once a
 * second, for the players whose addon asked for it, and tells the addon about each node that was
 * not there on the previous check: "XorWoW\tNODES;<name>,<name>..." (an addon whisper to the
 * player). The addon plays the sound the player picked in its options.
 *
 * Quest pickups are left out: many carry a herbalism or mining lock next to their plain "open" one
 * (Alterac Granite, Laden Mushroom, Mana Berry Bush...), so tracking shows them, but they hold
 * nothing without the quest. A node counts only when its loot has at least one item that drops
 * without a quest (or a reference); the quest-only loot ids are read from gameobject_loot_template
 * at startup, so a loot change needs a restart here too.
 *
 * The addon switches it on or off with "XorWoW\tNODES;on" / "XorWoW\tNODES;off", whispered to
 * itself and swallowed here; the server forgets it at logout, so the addon sends it at every login.
 */

#include "Chat.h"
#include "DBCStores.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Log.h"
#include "ObjectVisibilityContainer.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"

#include <string>
#include <unordered_set>

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr char NODES_KEY[] = "xorwow-gather-nodes";
    constexpr uint32 CHECK_INTERVAL_MS = 1000;
    constexpr size_t MAX_BODY = 240;   // an addon message is 255 bytes with the prefix

    struct NodeWatch : public DataMap::Base
    {
        bool enabled = false;
        bool primed = false;   // the first check only learns what is already around, silently
        uint32 timer = 0;
        std::unordered_set<ObjectGuid> seen;   // the tracked nodes present at the previous check
    };

    // Chest loot ids whose every item needs a quest; filled once at startup, read-only afterwards.
    std::unordered_set<uint32> questOnlyLoot;

    void LoadQuestOnlyLoot()
    {
        questOnlyLoot.clear();
        if (QueryResult result = WorldDatabase.Query("SELECT Entry FROM gameobject_loot_template GROUP BY Entry "
            "HAVING SUM(QuestRequired = 0 OR Reference <> 0) = 0"))
        {
            do
                questOnlyLoot.insert((*result)[0].Get<uint32>());
            while (result->NextRow());
        }
        LOG_INFO("server.loading", ">> XorWoW gathering alert: {} quest-only gameobject loot templates left out", questOnlyLoot.size());
    }

    // The resource tracking a node's lock answers to: Find Herbs or Find Minerals, else nothing
    // (also nothing for a quest pickup).
    uint32 TrackingBitFor(GameObject const* go)
    {
        if (questOnlyLoot.count(go->GetGOInfo()->GetLootId()))
            return 0;

        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lock = lockId ? sLockStore.LookupEntry(lockId) : nullptr;
        if (!lock)
            return 0;

        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
            if (lock->Type[i] == LOCK_KEY_SKILL && (lock->Index[i] == LOCKTYPE_HERBALISM || lock->Index[i] == LOCKTYPE_MINING))
                return uint32(1) << (lock->Index[i] - 1);
        return 0;
    }

    void SendToAddon(Player* player, std::string const& body)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\t{}", ADDON_PREFIX, body));
        player->SendDirectMessage(&data);
    }

    void Check(Player* player, NodeWatch& watch)
    {
        uint32 tracking = player->GetUInt32Value(PLAYER_TRACK_RESOURCES);
        std::unordered_set<ObjectGuid> present;
        std::string body = "NODES;";
        bool any = false;

        if (tracking)
        {
            if (VisibleWorldObjectsMap const* visible = player->GetObjectVisibilityContainer().GetVisibleWorldObjectsMap())
            {
                for (auto const& [guid, object] : *visible)
                {
                    GameObject const* go = object ? object->ToGameObject() : nullptr;
                    if (!go || !go->isSpawned() || go->getLootState() != GO_READY || !(tracking & TrackingBitFor(go)))
                        continue;

                    present.insert(guid);
                    if (watch.seen.count(guid))
                        continue;

                    std::string const& name = go->GetGOInfo()->name;
                    if (body.size() + name.size() + 1 <= MAX_BODY)
                    {
                        body += any ? "," : "";
                        body += name;
                    }
                    any = true;
                }
            }
        }

        watch.seen = std::move(present);
        bool primed = watch.primed;
        watch.primed = true;
        if (any && primed)
            SendToAddon(player, body);
    }
}

class xorwow_gather_nodes_playerscript : public PlayerScript
{
public:
    xorwow_gather_nodes_playerscript() : PlayerScript("xorwow_gather_nodes_playerscript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_UPDATE }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tNODES;", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        std::string state = msg.substr(prefix.size());
        if (state != "on" && state != "off")
            return true;

        NodeWatch* watch = player->CustomData.GetDefault<NodeWatch>(NODES_KEY);
        watch->enabled = state == "on";
        watch->seen.clear();
        watch->primed = false;
        return false;
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        NodeWatch* watch = player->CustomData.Get<NodeWatch>(NODES_KEY);
        if (!watch || !watch->enabled)
            return;

        watch->timer += diff;
        if (watch->timer < CHECK_INTERVAL_MS)
            return;
        watch->timer = 0;

        if (player->IsInWorld())
            Check(player, *watch);
    }
};

class xorwow_gather_nodes_worldscript : public WorldScript
{
public:
    xorwow_gather_nodes_worldscript() : WorldScript("xorwow_gather_nodes_worldscript", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        LoadQuestOnlyLoot();
    }
};

void AddSC_xorwow_gather_nodes()
{
    new xorwow_gather_nodes_playerscript();
    new xorwow_gather_nodes_worldscript();
}
