/*
 * XorWoW: guild hall build mode - furniture, lights, crafting stations, services and portals a
 * guild buys for its hall with the guild bank's gold, then moves or refunds.
 *
 * What can be placed is world.xorwow_guild_hall_catalog, generated with its gameobject_template
 * rows (260000 + item), the crafting stations' invisible spell focus objects (261001...) and the
 * service NPCs (creature 260000 + item) by scripts/client-patch/gen_guild_hall.py from
 * client-patch/guild_hall_catalog.py, which also writes the XorWoW addon's list.
 * What was placed is characters.xorwow_guild_hall_object, one row per object, kept in memory.
 *
 * Halls are instances that come and go (xorwow_guild_hall.cpp): the objects are spawned as
 * temporary objects, never saved as world spawns, when the first player enters an instance, and
 * vanish with it. The whole hall is one grid, loaded while anyone is inside.
 *
 * Only members whose rank may edit the hall (CanEditGuildHall) build, inside their own guild's
 * hall. The XorWoW addon's build mode talks to this script over addon whispers to itself:
 *   "GHB;ON" / "GHB;OFF"            enter / leave build mode: the placement spell is learned for
 *                                    the session (not saved) and unlearned
 *   "GHB?"                           -> the state
 *   "GHB;AIM;<item>;<scale>;<rot>"  the next placement buys this item (scale in %, rot in degrees)
 *   "GHB;MOVE;<object>;<scale>;<rot>" the next placement moves this object instead
 *   "GHB;REFUND;<object>"            removes it, its price back into the guild bank
 *   "GHB;GRAB;<name>"                the next placement moves the hall's object of that name the
 *                                    player points at (the addon's right-click while the tooltip
 *                                    shows a placed object: the cast needs that click, so the
 *                                    client sends no use request to tell which one it was)
 *   "GHB;FIND;<name>"                -> "SEL;..." for that object (Shift + right-click: remove)
 *   "GHB;TURN;<degrees>;<name>"      turns that object where it stands (Shift + wheel over it)
 *   "GHB;EMBLEM?"                    -> "EMBLEM;<guild id>;<style>;<color>;<border>;<border color>;<background>",
 *                                    "EMBLEM;0" outside a guild: the addon keeps it for the launcher,
 *                                    which paints the guild banners' textures from it (Core\GuildBanners.cs)
 * and answers
 *   "GHB;STATE;<building>;<may edit>;<in own hall>;<guild bank copper>;<placed>;<limit>;<may tune>"
 *     may tune: the account is in XorWoW.GuildHall.PreviewTuners (worldserver.conf, comma list,
 *     default XORBIS): the addon's preview tuning keys, for framing new catalogue models
 *   "GHB;COUNT;<group>=<n>,..."      placed per limited group, for the "1 max" items
 *   "GHB;SEL;<object>;<item>;<scale>;<rot>;<refund copper>"  right-clicked in build mode: the addon
 *                                    picks it up (MOVE), or with Shift asks to remove it (REFUND)
 *   "GHB;DONE;<text>" / "GHB;ERR;<text>"
 * The addon's build panel buttons cast Guild Hall Placement (spell 260010, the client patch's,
 * Blizzard's ground targeting circle): the spot clicked on the floor is the spell's destination.
 *
 * In build mode a right-click on a placed object - a chair, a mailbox, the guild vault, a banker -
 * selects it instead of using it: the use, gossip, bank, auction and mail requests the client
 * sends for it are taken here and never reach the core.
 *
 * Threads: the addon messages arrive on the world thread; the spell lands on its map's thread, so
 * a placement waits for the world update (no map updates then) to be paid and spawned; a
 * right-click may come on either. Everything below is under one lock.
 */

#include "AccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameObjectScript.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "Tokenize.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

bool CanEditGuildHall(Player* player);
bool IsGuildHallMap(uint32 mapId);
bool IsInOwnGuildHall(Player* player);
uint32 GuildOfHallInstance(uint32 mapId, uint32 instanceId);
bool NearGuildHallEntrance(uint32 mapId, Position const& pos, float distance);

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr char TAG[] = "GHB";
    constexpr char PENDING_KEY[] = "xorwow-guild-hall-build";
    constexpr uint32 PLACEMENT_SPELL = 260010;
    constexpr uint32 HALL_OBJECT_LIMIT = 400;   // per guild: what one instance spawns at its first visit
    constexpr float ENTRANCE_CLEARANCE = 4.0f;  // yards kept free around the Guildstone's landing spot
    constexpr uint32 SELECT_REPEAT_MS = 500;    // one right-click sends several requests
    constexpr uint32 GUILD_BANNER_SLOTS = 999;  // guild banner displays per model (client-patch/patch.json guild_banners)

    enum CatalogTeam : uint8 { CATALOG_BOTH = 0, CATALOG_ALLIANCE = 1, CATALOG_HORDE = 2 };

    struct CatalogItem
    {
        uint32 id = 0;
        std::string name;
        uint32 price = 0;
        uint8 team = CATALOG_BOTH;
        std::string group;      // items sharing maxCount; the item's own id when empty
        uint32 maxCount = 0;    // 0 = only the hall's limit
        uint32 goEntry = 0;
        uint32 focusEntry = 0;
        uint32 npcEntry = 0;
        int32 destMap = -1;
        Position dest;
        bool enabled = true;
        float rise = 0.0f;      // yards the object stands above where it is placed: models centred on their origin
        uint32 guildDisplay = 0; // guild banners: the guild's own display is this + its guild id
    };

    struct PlacedObject
    {
        uint32 id = 0;
        uint32 guildId = 0;
        uint32 mapId = 0;
        uint32 item = 0;
        Position pos;
        uint32 scale = 100;     // %
        uint32 price = 0;       // copper paid: what a refund gives back
    };

    // The objects spawned in one hall instance.
    struct Spawned
    {
        uint32 guildId = 0;
        std::unordered_map<uint32, std::vector<ObjectGuid>> guids;   // object id -> its world objects
        std::unordered_map<ObjectGuid, uint32> objectOf;              // world object -> object id
    };

    // A builder's next placement, from the addon; on the player (CustomData).
    struct Pending : public DataMap::Base
    {
        uint32 item = 0;
        uint32 moveObject = 0;
        uint32 scale = 100;
        int32 rotation = 0;     // degrees, added to the object's facing
        uint32 lastSelect = 0;  // getMSTime() of the last right-click selection
        uint32 lastObject = 0;
    };

    struct Placement
    {
        ObjectGuid player;
        Position dest;
    };

    std::mutex lock;
    std::unordered_map<uint32, CatalogItem> catalog;
    std::unordered_map<uint32, std::map<uint32, PlacedObject>> objects;   // guild -> object id -> object
    uint32 nextObjectId = 1;
    std::unordered_map<uint64, Spawned> spawned;                          // (map id << 32 | instance id)
    // Players in build mode. Their own lock: GameObject::XorWoWInteractCheck reads it while update
    // packets are built, which spawning (under `lock`) can set off.
    std::mutex builderLock;
    std::unordered_set<ObjectGuid> builders;
    std::vector<Placement> placements;                                    // from the spell, for the world update
    std::unordered_map<uint32, bool> tuners;                              // account id -> may tune previews

    uint64 InstanceKey(uint32 mapId, uint32 instanceId)
    {
        return (uint64(mapId) << 32) | instanceId;
    }

    std::string GroupOf(CatalogItem const& item)
    {
        return item.group.empty() ? std::to_string(item.id) : item.group;
    }

    CatalogItem const* FindItem(uint32 id)
    {
        auto it = catalog.find(id);
        return it == catalog.end() ? nullptr : &it->second;
    }

    void SendToAddon(Player* player, std::string const& body)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\t{};{}", ADDON_PREFIX, TAG, body));
        player->SendDirectMessage(&data);
    }

    void SendError(Player* player, std::string const& text)
    {
        SendToAddon(player, "ERR;" + text);
    }

    bool IsBuilder(Player* player)
    {
        std::lock_guard<std::mutex> guard(builderLock);
        return builders.count(player->GetGUID()) != 0;
    }

    uint32 PlacedCount(uint32 guildId)
    {
        auto it = objects.find(guildId);
        return it == objects.end() ? 0 : uint32(it->second.size());
    }

    // ---------------------------------------------------------------------------------------------
    // Spawning: temporary objects in the instance, nothing saved as a world spawn.

    // displayId: 0 = the template's
    GameObject* SpawnGameObject(Map* map, uint32 entry, Position const& pos, float scale, uint32 displayId = 0)
    {
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(entry);
        if (!info)
            return nullptr;

        GameObject* go = new GameObject();
        G3D::Quat rotation = G3D::Quat::fromAxisAngleRotation(G3D::Vector3::unitZ(), pos.GetOrientation());
        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), entry, map, PHASEMASK_NORMAL,
                        pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(), rotation, 255, GO_STATE_READY))
        {
            delete go;
            return nullptr;
        }
        go->SetObjectScale(info->size * scale);
        if (displayId)
            go->SetDisplayId(displayId);
        go->SetRespawnTime(0);
        go->SetSpawnedByDefault(false);
        if (!map->AddToMap(go))
        {
            delete go;
            return nullptr;
        }
        return go;
    }

    // The object's world objects: the object itself, or the NPC; a crafting station's spell focus.
    std::vector<ObjectGuid> SpawnObject(Map* map, PlacedObject const& object, CatalogItem const& item)
    {
        std::vector<ObjectGuid> guids;
        float scale = object.scale / 100.0f;
        if (item.npcEntry)
        {
            if (TempSummon* npc = map->SummonCreature(item.npcEntry, object.pos))
            {
                npc->SetObjectScale(npc->GetObjectScale() * scale);
                npc->SetHomePosition(object.pos);
                npc->SetReactState(REACT_PASSIVE);
                guids.push_back(npc->GetGUID());
            }
        }
        else
        {
            Position pos = object.pos;
            pos.m_positionZ += item.rise * scale;
            // a guild banner: the guild's tabard, its client-side textures painted by the launcher
            uint32 displayId = item.guildDisplay && object.guildId <= GUILD_BANNER_SLOTS ? item.guildDisplay + object.guildId : 0;
            if (GameObject* go = SpawnGameObject(map, item.goEntry, pos, scale, displayId))
                guids.push_back(go->GetGUID());
        }

        if (!guids.empty() && item.focusEntry)
            if (GameObject* focus = SpawnGameObject(map, item.focusEntry, object.pos, 1.0f))
                guids.push_back(focus->GetGUID());
        return guids;
    }

    void Despawn(Map* map, std::vector<ObjectGuid> const& guids)
    {
        for (ObjectGuid const& guid : guids)
        {
            if (guid.IsGameObject())
            {
                // not DespawnOrUnsummon: a goober or chair the core did not summon from a spell is
                // only reset by it, and stays
                if (GameObject* go = map->GetGameObject(guid))
                {
                    go->SetRespawnTime(0);
                    go->Delete();
                }
            }
            else if (Creature* npc = map->GetCreature(guid))
                npc->DespawnOrUnsummon();
        }
    }

    // Under the lock: spawns the object in every loaded instance of its guild's hall.
    void SpawnEverywhere(PlacedObject const& object)
    {
        CatalogItem const* item = FindItem(object.item);
        if (!item)
            return;
        for (auto& [key, instance] : spawned)
        {
            if (instance.guildId != object.guildId || uint32(key >> 32) != object.mapId)
                continue;
            Map* map = sMapMgr->FindMap(object.mapId, uint32(key & 0xFFFFFFFF));
            if (!map)
                continue;
            std::vector<ObjectGuid> guids = SpawnObject(map, object, *item);
            for (ObjectGuid const& guid : guids)
                instance.objectOf[guid] = object.id;
            instance.guids[object.id] = std::move(guids);
        }
    }

    // Under the lock: removes the object's world objects from every instance.
    void DespawnEverywhere(PlacedObject const& object)
    {
        for (auto& [key, instance] : spawned)
        {
            if (instance.guildId != object.guildId)
                continue;
            auto it = instance.guids.find(object.id);
            if (it == instance.guids.end())
                continue;
            if (Map* map = sMapMgr->FindMap(uint32(key >> 32), uint32(key & 0xFFFFFFFF)))
                Despawn(map, it->second);
            for (ObjectGuid const& guid : it->second)
                instance.objectOf.erase(guid);
            instance.guids.erase(it);
        }
    }

    // A player entered a hall instance (its map thread): its first visitor spawns the guild's
    // objects; later ones put back any that went missing.
    void Populate(Map* map)
    {
        uint32 guildId = GuildOfHallInstance(map->GetId(), map->GetInstanceId());
        if (!guildId)
            return;

        std::lock_guard<std::mutex> guard(lock);
        Spawned& instance = spawned[InstanceKey(map->GetId(), map->GetInstanceId())];
        instance.guildId = guildId;
        auto placed = objects.find(guildId);
        if (placed == objects.end())
            return;

        for (auto const& [id, object] : placed->second)
        {
            if (object.mapId != map->GetId())
                continue;
            auto it = instance.guids.find(id);
            if (it != instance.guids.end())
            {
                bool present = !it->second.empty() && (it->second.front().IsGameObject()
                    ? map->GetGameObject(it->second.front()) != nullptr : map->GetCreature(it->second.front()) != nullptr);
                if (present)
                    continue;
                for (ObjectGuid const& guid : it->second)
                    instance.objectOf.erase(guid);
            }
            CatalogItem const* item = FindItem(object.item);
            if (!item)
                continue;
            std::vector<ObjectGuid> guids = SpawnObject(map, object, *item);
            for (ObjectGuid const& guid : guids)
                instance.objectOf[guid] = id;
            instance.guids[id] = std::move(guids);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // What the addon is told.

    // The realm admin's accounts: the addon's preview tuning keys (XorWoW.GuildHall.PreviewTuners).
    bool MayTunePreviews(Player* player)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = tuners.find(accountId);
            if (it != tuners.end())
                return it->second;
        }
        std::string name;
        bool allowed = false;
        if (AccountMgr::GetName(accountId, name))
        {
            std::string list = sConfigMgr->GetOption<std::string>("XorWoW.GuildHall.PreviewTuners", "XORBIS");
            for (std::string_view entry : Acore::Tokenize(list, ',', false))
            {
                std::string tuner(entry);
                tuner.erase(0, tuner.find_first_not_of(' '));
                tuner.erase(tuner.find_last_not_of(' ') + 1);
                if (!tuner.empty() && StringEqualI(tuner, name))
                    allowed = true;
            }
        }
        std::lock_guard<std::mutex> guard(lock);
        tuners[accountId] = allowed;
        return allowed;
    }

    // The guild's tabard design, for the launcher's guild banner textures (by way of the addon).
    void SendEmblem(Player* player)
    {
        Guild* guild = player->GetGuild();
        if (!guild)
        {
            SendToAddon(player, "EMBLEM;0");
            return;
        }
        EmblemInfo const& emblem = guild->GetEmblemInfo();
        SendToAddon(player, Acore::StringFormat("EMBLEM;{};{};{};{};{};{}", guild->GetId(), emblem.GetStyle(), emblem.GetColor(),
            emblem.GetBorderStyle(), emblem.GetBorderColor(), emblem.GetBackgroundColor()));
    }

    void SendState(Player* player)
    {
        Guild* guild = player->GetGuild();
        uint32 placed;
        {
            std::lock_guard<std::mutex> guard(lock);
            placed = guild ? PlacedCount(guild->GetId()) : 0;
        }
        bool building = IsBuilder(player);
        SendToAddon(player, Acore::StringFormat("STATE;{};{};{};{};{};{};{}", building ? 1 : 0, CanEditGuildHall(player) ? 1 : 0,
            IsInOwnGuildHall(player) ? 1 : 0, guild ? guild->GetTotalBankMoney() : 0, placed, HALL_OBJECT_LIMIT,
            MayTunePreviews(player) ? 1 : 0));
    }

    // The limited groups' counts, in as many messages as it takes (an addon message is 255 bytes).
    void SendCounts(Player* player)
    {
        Guild* guild = player->GetGuild();
        std::map<std::string, uint32> counts;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (guild)
            {
                auto placed = objects.find(guild->GetId());
                if (placed != objects.end())
                    for (auto const& [id, object] : placed->second)
                        if (CatalogItem const* item = FindItem(object.item); item && item->maxCount)
                            ++counts[GroupOf(*item)];
            }
        }
        std::string line;
        for (auto const& [group, count] : counts)
        {
            std::string entry = Acore::StringFormat("{}={}", group, count);
            if (line.size() + entry.size() > 200)
            {
                SendToAddon(player, "COUNT;" + line);
                line.clear();
            }
            line += (line.empty() ? "" : ",") + entry;
        }
        SendToAddon(player, "COUNT;" + line);   // an empty one too: nothing limited is placed
    }

    // Every builder of the guild: the money and the counts changed.
    void UpdateBuilders(uint32 guildId)
    {
        std::vector<ObjectGuid> guids;
        {
            std::lock_guard<std::mutex> guard(builderLock);
            guids.assign(builders.begin(), builders.end());
        }
        for (ObjectGuid const& guid : guids)
            if (Player* player = ObjectAccessor::FindPlayer(guid))
                if (player->GetGuildId() == guildId)
                {
                    SendState(player);
                    SendCounts(player);
                }
    }

    // ---------------------------------------------------------------------------------------------
    // Build mode.

    // The player's view of the hall's furniture: clickable in build mode only (GO_FLAG_INTERACT_COND
    // + GameObject::XorWoWInteractCheck); its dynamic flags are sent again. No map update running.
    void RefreshInteraction(Player* player)
    {
        std::vector<ObjectGuid> guids;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto instance = spawned.find(InstanceKey(player->GetMapId(), player->GetInstanceId()));
            if (instance == spawned.end())
                return;
            for (auto const& [guid, id] : instance->second.objectOf)
                if (guid.IsGameObject())
                    guids.push_back(guid);
        }
        for (ObjectGuid const& guid : guids)
            if (GameObject* go = player->GetMap()->GetGameObject(guid))
                go->ForceValuesUpdateAtIndex(GAMEOBJECT_DYNAMIC);
    }

    void LeaveBuildMode(Player* player)
    {
        {
            std::lock_guard<std::mutex> guard(builderLock);
            builders.erase(player->GetGUID());
        }
        if (player->HasSpell(PLACEMENT_SPELL))
            player->removeSpell(PLACEMENT_SPELL, SPEC_MASK_ALL, false);
        if (Pending* pending = player->CustomData.Get<Pending>(PENDING_KEY))
            pending->item = pending->moveObject = 0;
    }

    void EnterBuildMode(Player* player)
    {
        if (!IsInOwnGuildHall(player))
        {
            SendError(player, "Build mode only works inside your guild's hall.");
            SendState(player);
            return;
        }
        if (!CanEditGuildHall(player))
        {
            SendError(player, "Your guild rank may not edit the guild hall.");
            SendState(player);
            return;
        }
        {
            std::lock_guard<std::mutex> guard(builderLock);
            builders.insert(player->GetGUID());
        }
        if (!player->HasSpell(PLACEMENT_SPELL))
            player->learnSpell(PLACEMENT_SPELL, true);   // temporary: never saved with the character
        RefreshInteraction(player);
        SendState(player);
        SendCounts(player);
    }

    // Why this builder may not build right now; empty = may.
    std::string BuildRefusal(Player* player)
    {
        if (!IsInOwnGuildHall(player))
            return "You are not in your guild's hall.";
        if (!CanEditGuildHall(player))
            return "Your guild rank may not edit the guild hall.";
        if (!IsBuilder(player))
            return "You are not in build mode.";
        return "";
    }

    // "<item or object>;<scale>;<rot>"
    void SetPending(Player* player, std::string_view args, bool move)
    {
        std::vector<std::string_view> fields = Acore::Tokenize(args, ';', false);
        if (fields.size() != 3)
            return;
        std::optional<uint32> id = Acore::StringTo<uint32>(fields[0]);
        std::optional<uint32> scale = Acore::StringTo<uint32>(fields[1]);
        std::optional<int32> rotation = Acore::StringTo<int32>(fields[2]);
        if (!id || !scale || !rotation)
            return;
        Pending* pending = player->CustomData.GetDefault<Pending>(PENDING_KEY);
        pending->item = move ? 0 : *id;
        pending->moveObject = move ? *id : 0;
        pending->scale = 100;   // real size only: a scaled chair or bed does not match where one sits (user, 2026-10-02)
        pending->rotation = 0;   // no turning: the object faces the camera as the preview does (user, 2026-10-02)
    }

    std::string MoneyText(uint32 copper)
    {
        std::string text;
        if (copper >= GOLD)
            text += Acore::StringFormat("{}g", copper / GOLD);
        if (copper % GOLD >= SILVER)
            text += Acore::StringFormat("{}{}s", text.empty() ? "" : " ", copper % GOLD / SILVER);
        if (copper % SILVER || text.empty())
            text += Acore::StringFormat("{}{}c", text.empty() ? "" : " ", copper % SILVER);
        return text;
    }

    // World thread, no map update running.
    void Refund(Player* player, uint32 objectId)
    {
        std::string refusal = BuildRefusal(player);
        if (!refusal.empty())
        {
            SendError(player, refusal);
            return;
        }
        Guild* guild = player->GetGuild();
        PlacedObject object;
        std::string name;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto placed = objects.find(guild->GetId());
            if (placed == objects.end() || !placed->second.count(objectId))
            {
                SendError(player, "That object is gone already.");
                return;
            }
            object = placed->second[objectId];
            CatalogItem const* item = FindItem(object.item);
            name = item ? item->name : "object";
        }

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (object.price && !guild->HandleGuildHallPayment(trans, player->GetGUID(), object.price, true))
        {
            SendError(player, "The guild bank is full: it cannot take the refund.");
            return;
        }
        trans->Append("DELETE FROM xorwow_guild_hall_object WHERE id = {}", objectId);
        CharacterDatabase.CommitTransaction(trans);

        {
            std::lock_guard<std::mutex> guard(lock);
            DespawnEverywhere(object);
            objects[guild->GetId()].erase(objectId);
        }
        SendToAddon(player, Acore::StringFormat("DONE;{} refunded: {} back in the guild bank.", name, MoneyText(object.price)));
        UpdateBuilders(guild->GetId());
    }

    // The guild's placed object of this name the player points at: of those on the map, the one
    // closest to the line ahead of the player (the camera's, turned with the right mouse button),
    // nearer ones first. Names are not unique; this is the best guess the client allows.
    std::optional<PlacedObject> ObjectByName(Player* player, std::string_view name)
    {
        std::optional<PlacedObject> best;
        float bestScore = 0.0f;
        std::lock_guard<std::mutex> guard(lock);
        auto placed = objects.find(player->GetGuildId());
        if (placed == objects.end())
            return best;
        for (auto const& [id, object] : placed->second)
        {
            CatalogItem const* item = FindItem(object.item);
            if (!item || object.mapId != player->GetMapId())
                continue;
            // the tooltip shows a service NPC's own name ("Dancer" for every dancer race)
            CreatureTemplate const* npc = item->npcEntry ? sObjectMgr->GetCreatureTemplate(item->npcEntry) : nullptr;
            if (!StringEqualI(item->name, name) && !StringEqualI("Guild " + item->name, name) && !(npc && StringEqualI(npc->Name, name)))
                continue;
            float distance = player->GetExactDist2d(&object.pos);
            float off = std::fabs(Position::NormalizeOrientation(player->GetAngle(&object.pos) - player->GetOrientation()));
            if (off > float(M_PI))
                off = 2 * float(M_PI) - off;
            float score = distance * (1.0f + 4.0f * off / float(M_PI));
            if (!best || score < bestScore)
            {
                best = object;
                bestScore = score;
            }
        }
        return best;
    }

    // Under the lock: the object turned where it stands. Tried and dropped (2026-10-02): turning the
    // object in place (its parent rotation field: clients ignore it on a standing object) and sending
    // it again (a client keeps its copy, old angle). So the turned one is a new object, spawned
    // first; the old one then goes at once, without the despawn animation (its fade out was too
    // slow). The new one's short fade in is the client's own. An NPC just turns.
    void TurnEverywhere(PlacedObject const& object)
    {
        // the object stands its rise above its spot, as SpawnObject puts it; a crafting station's
        // focus stays on the spot
        CatalogItem const* item = FindItem(object.item);
        Position raised = object.pos;
        if (item)
            raised.m_positionZ += item->rise * object.scale / 100.0f;
        for (auto& [key, instance] : spawned)
        {
            if (instance.guildId != object.guildId)
                continue;
            auto it = instance.guids.find(object.id);
            Map* map = it == instance.guids.end() ? nullptr : sMapMgr->FindMap(uint32(key >> 32), uint32(key & 0xFFFFFFFF));
            if (!map)
                continue;
            std::vector<ObjectGuid> kept;
            for (ObjectGuid const& guid : it->second)
            {
                if (!guid.IsGameObject())
                {
                    if (Creature* npc = map->GetCreature(guid))
                    {
                        npc->SetHomePosition(object.pos);
                        npc->SetFacingTo(object.pos.GetOrientation());
                    }
                    kept.push_back(guid);
                    continue;
                }
                GameObject* old = map->GetGameObject(guid);
                if (!old)
                    continue;
                bool focus = item && item->focusEntry && old->GetEntry() == item->focusEntry;
                GameObject* turned = SpawnGameObject(map, old->GetEntry(), focus ? object.pos : raised,old->GetObjectScale() / old->GetGOInfo()->size, old->GetDisplayId());
                if (!turned)
                {
                    kept.push_back(guid);
                    continue;
                }
                map->DoForAllPlayers([old](Player* player)
                {
                    if (player->HaveAtClient(old))
                        old->DestroyForPlayer(player);
                });
                old->SetRespawnTime(0);
                old->AddObjectToRemoveList();
                instance.objectOf.erase(guid);
                instance.objectOf[turned->GetGUID()] = object.id;
                kept.push_back(turned->GetGUID());
            }
            it->second = std::move(kept);
        }
    }

    // Shift + wheel over a placed object: turned where it stands and saved. World thread, no map
    // update running.
    void Turn(Player* player, int32 degrees, std::string_view name)
    {
        std::string refusal = BuildRefusal(player);
        if (!refusal.empty())
        {
            SendError(player, refusal);
            return;
        }
        std::optional<PlacedObject> found = ObjectByName(player, name);
        if (!found)
        {
            SendError(player, "That is not one of your hall's objects.");
            return;
        }
        PlacedObject object;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto placed = objects.find(found->guildId);
            if (placed == objects.end() || !placed->second.count(found->id))
                return;
            PlacedObject& stored = placed->second[found->id];
            stored.pos.SetOrientation(Position::NormalizeOrientation(stored.pos.GetOrientation() + degrees * float(M_PI) / 180.0f));
            object = stored;
            TurnEverywhere(object);
        }
        CharacterDatabase.Execute("UPDATE xorwow_guild_hall_object SET o = {} WHERE id = {}", object.pos.GetOrientation(), object.id);
    }

    // The addon's removal confirmation for this object (and its fallback pick-up).
    void SendSelection(Player* player, PlacedObject const& object)
    {
        int32 facing = int32(std::lround(object.pos.GetOrientation() * 180.0f / float(M_PI)));
        SendToAddon(player, Acore::StringFormat("SEL;{};{};{};{};{}", object.id, object.item, object.scale, facing, object.price));
    }

    // World thread, no map update running: the spell's placement, paid and spawned.
    void Place(Player* player, Position dest)
    {
        std::string refusal = BuildRefusal(player);
        if (!refusal.empty())
        {
            SendError(player, refusal);
            return;
        }
        Pending* pending = player->CustomData.Get<Pending>(PENDING_KEY);
        if (!pending || (!pending->item && !pending->moveObject))
        {
            SendError(player, "Pick something in the build panel first.");
            return;
        }
        if (NearGuildHallEntrance(player->GetMapId(), dest, ENTRANCE_CLEARANCE))
        {
            SendError(player, "Keep the entrance clear: members arrive there.");
            return;
        }

        Guild* guild = player->GetGuild();
        uint32 guildId = guild->GetId();
        // As the addon's preview shows it, whatever the spot: facing back along the player's facing -
        // the camera's when it is turned with the right mouse button (the client tells no one where
        // a left-button camera looks) - a quarter turn counter-clockwise (the preview frame's front is
        // the model's side; seen in game 2026-10-02).
        dest.SetOrientation(Position::NormalizeOrientation(player->GetOrientation() + float(M_PI) * 1.5f + pending->rotation * float(M_PI) / 180.0f));
        uint32 scale = pending->scale;
        uint32 moveObject = pending->moveObject;
        uint32 itemId = pending->item;
        pending->item = pending->moveObject = 0;

        if (moveObject)
        {
            PlacedObject object;
            {
                std::lock_guard<std::mutex> guard(lock);
                auto placed = objects.find(guildId);
                if (placed == objects.end() || !placed->second.count(moveObject))
                {
                    SendError(player, "That object is gone already.");
                    return;
                }
                PlacedObject& stored = placed->second[moveObject];
                DespawnEverywhere(stored);
                // a moved object keeps the angle it was turned to (user, 2026-10-02)
                stored.pos.Relocate(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
                stored.scale = scale;
                stored.mapId = player->GetMapId();
                object = stored;
                SpawnEverywhere(object);
            }
            CharacterDatabase.Execute("UPDATE xorwow_guild_hall_object SET map = {}, x = {}, y = {}, z = {}, o = {}, scale = {} WHERE id = {}",
                object.mapId, object.pos.GetPositionX(), object.pos.GetPositionY(), object.pos.GetPositionZ(), object.pos.GetOrientation(), object.scale, object.id);
            SendToAddon(player, "DONE;Moved.");
            return;
        }

        CatalogItem item;
        {
            std::lock_guard<std::mutex> guard(lock);
            CatalogItem const* found = FindItem(itemId);
            if (!found || !found->enabled)
            {
                SendError(player, "That cannot be placed.");
                return;
            }
            item = *found;
            if ((item.team == CATALOG_ALLIANCE && player->GetTeamId() != TEAM_ALLIANCE)
                || (item.team == CATALOG_HORDE && player->GetTeamId() != TEAM_HORDE))
            {
                SendError(player, "That is for the other faction's halls.");
                return;
            }
            if (item.guildDisplay && guildId > GUILD_BANNER_SLOTS)
            {
                SendError(player, "Guild banners are not available to your guild.");
                return;
            }
            if (PlacedCount(guildId) >= HALL_OBJECT_LIMIT)
            {
                SendError(player, Acore::StringFormat("The hall is full: {} objects at most.", HALL_OBJECT_LIMIT));
                return;
            }
            if (item.maxCount)
            {
                uint32 count = 0;
                std::string group = GroupOf(item);
                for (auto const& [id, object] : objects[guildId])
                    if (CatalogItem const* other = FindItem(object.item); other && GroupOf(*other) == group)
                        ++count;
                if (count >= item.maxCount)
                {
                    SendError(player, Acore::StringFormat("Your hall has {} already: {} at most.", item.name, item.maxCount));
                    return;
                }
            }
        }
        if (guild->GetTotalBankMoney() < item.price)
        {
            SendError(player, Acore::StringFormat("{} costs {}: the guild bank has {}.", item.name, MoneyText(item.price), MoneyText(uint32(guild->GetTotalBankMoney()))));
            return;
        }

        PlacedObject object;
        object.guildId = guildId;
        object.mapId = player->GetMapId();
        object.item = item.id;
        object.pos = dest;
        object.scale = scale;
        object.price = item.price;
        {
            std::lock_guard<std::mutex> guard(lock);
            object.id = nextObjectId++;
        }

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (!guild->HandleGuildHallPayment(trans, player->GetGUID(), item.price, false))
        {
            SendError(player, Acore::StringFormat("{} costs {}: the guild bank has {}.", item.name, MoneyText(item.price), MoneyText(uint32(guild->GetTotalBankMoney()))));
            return;
        }
        trans->Append("INSERT INTO xorwow_guild_hall_object (id, guildid, map, item, x, y, z, o, scale, price, placed_by, placed_at) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, UNIX_TIMESTAMP())",
            object.id, object.guildId, object.mapId, object.item, object.pos.GetPositionX(), object.pos.GetPositionY(), object.pos.GetPositionZ(),
            object.pos.GetOrientation(), object.scale, object.price, player->GetGUID().GetCounter());
        CharacterDatabase.CommitTransaction(trans);

        {
            std::lock_guard<std::mutex> guard(lock);
            objects[guildId][object.id] = object;
            SpawnEverywhere(object);
        }
        SendToAddon(player, Acore::StringFormat("DONE;{} placed for {}.", item.name, MoneyText(item.price)));
        UpdateBuilders(guildId);
    }

    // A right-click in build mode on something placed: the addon's Move / Refund menu.
    // Any thread; true = the request was ours, drop it.
    bool Select(Player* player, ObjectGuid guid)
    {
        uint32 objectId = 0;
        PlacedObject object;
        if (!IsBuilder(player))
            return false;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto instance = spawned.find(InstanceKey(player->GetMapId(), player->GetInstanceId()));
            if (instance == spawned.end())
                return false;
            auto it = instance->second.objectOf.find(guid);
            if (it == instance->second.objectOf.end())
                return false;
            objectId = it->second;
            auto placed = objects.find(instance->second.guildId);
            if (placed == objects.end() || !placed->second.count(objectId))
                return true;
            object = placed->second[objectId];
        }

        Pending* pending = player->CustomData.GetDefault<Pending>(PENDING_KEY);
        uint32 now = getMSTime();
        if (pending->lastObject == objectId && getMSTimeDiff(pending->lastSelect, now) < SELECT_REPEAT_MS)
            return true;
        pending->lastObject = objectId;
        pending->lastSelect = now;

        SendSelection(player, object);
        return true;
    }

    void LoadCatalog()
    {
        std::lock_guard<std::mutex> guard(lock);
        catalog.clear();
        QueryResult result = WorldDatabase.Query("SELECT id, name, price, team, limit_group, max_count, go_entry, focus_entry, npc_entry, "
            "dest_map, dest_x, dest_y, dest_z, dest_o, enabled, rise, guild_display FROM xorwow_guild_hall_catalog");
        if (!result)
        {
            LOG_ERROR("server.loading", "XorWoW guild hall: world.xorwow_guild_hall_catalog is empty or missing - build mode has nothing to place.");
            return;
        }
        do
        {
            Field* f = result->Fetch();
            CatalogItem item;
            item.id = f[0].Get<uint32>();
            item.name = f[1].Get<std::string>();
            item.price = f[2].Get<uint32>();
            item.team = f[3].Get<uint8>();
            item.group = f[4].Get<std::string>();
            item.maxCount = f[5].Get<uint32>();
            item.goEntry = f[6].Get<uint32>();
            item.focusEntry = f[7].Get<uint32>();
            item.npcEntry = f[8].Get<uint32>();
            item.destMap = f[9].Get<int32>();
            item.dest.Relocate(f[10].Get<float>(), f[11].Get<float>(), f[12].Get<float>(), f[13].Get<float>());
            item.enabled = f[14].Get<uint8>() != 0;
            item.rise = f[15].Get<float>();
            item.guildDisplay = f[16].Get<uint32>();
            if (item.goEntry && !sObjectMgr->GetGameObjectTemplate(item.goEntry))
                LOG_ERROR("server.loading", "XorWoW guild hall: item {} ({}) has no gameobject_template {}", item.id, item.name, item.goEntry);
            if (item.npcEntry && !sObjectMgr->GetCreatureTemplate(item.npcEntry))
                LOG_ERROR("server.loading", "XorWoW guild hall: item {} ({}) has no creature_template {}", item.id, item.name, item.npcEntry);
            catalog[item.id] = std::move(item);
        } while (result->NextRow());
    }

    void LoadObjects()
    {
        std::lock_guard<std::mutex> guard(lock);
        objects.clear();
        nextObjectId = 1;
        QueryResult result = CharacterDatabase.Query("SELECT id, guildid, map, item, x, y, z, o, scale, price FROM xorwow_guild_hall_object");
        if (!result)
            return;
        do
        {
            Field* f = result->Fetch();
            PlacedObject object;
            object.id = f[0].Get<uint32>();
            object.guildId = f[1].Get<uint32>();
            object.mapId = f[2].Get<uint32>();
            object.item = f[3].Get<uint32>();
            object.pos.Relocate(f[4].Get<float>(), f[5].Get<float>(), f[6].Get<float>(), f[7].Get<float>());
            object.scale = f[8].Get<uint32>();
            object.price = f[9].Get<uint32>();
            objects[object.guildId][object.id] = object;
            nextObjectId = std::max(nextObjectId, object.id + 1);
        } while (result->NextRow());
    }

    // The client sends one of these for a right-click, depending on what was clicked.
    bool IsUseRequest(uint16 opcode)
    {
        switch (opcode)
        {
            case CMSG_GAMEOBJ_USE:
            case CMSG_GAMEOBJ_REPORT_USE:
            case CMSG_GOSSIP_HELLO:
            case CMSG_BANKER_ACTIVATE:
            case MSG_AUCTION_HELLO:
            case CMSG_GUILD_BANKER_ACTIVATE:
            case CMSG_GUILD_BANK_QUERY_TAB:
            case CMSG_GET_MAIL_LIST:
            case CMSG_QUESTGIVER_HELLO:
                return true;
            default:
                return false;
        }
    }
}

// Guild Hall Placement (260010): the spot clicked on the floor.
class spell_xorwow_guild_hall_placement : public SpellScript
{
    PrepareSpellScript(spell_xorwow_guild_hall_placement)

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return SPELL_FAILED_DONT_REPORT;
        std::string refusal = BuildRefusal(player);
        if (!refusal.empty())
        {
            ChatHandler(player->GetSession()).SendNotification("{}", refusal);
            return SPELL_FAILED_DONT_REPORT;
        }
        Pending* pending = player->CustomData.Get<Pending>(PENDING_KEY);
        if (!pending || (!pending->item && !pending->moveObject))
        {
            ChatHandler(player->GetSession()).SendNotification("Pick something in the build panel first.");
            return SPELL_FAILED_DONT_REPORT;
        }
        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        // the destination read from the client has no map id: it is always on the caster's map
        WorldLocation const* dest = GetExplTargetDest();
        if (!player || !dest)
            return;
        std::lock_guard<std::mutex> guard(lock);
        placements.push_back({ player->GetGUID(), Position(dest->GetPositionX(), dest->GetPositionY(), dest->GetPositionZ()) });
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_xorwow_guild_hall_placement::CheckCast);
        OnEffectHit += SpellEffectFn(spell_xorwow_guild_hall_placement::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// Furniture, lights, crafting stations: nothing happens on a click (outside build mode).
class xorwow_guild_hall_decor : public GameObjectScript
{
public:
    xorwow_guild_hall_decor() : GameObjectScript("xorwow_guild_hall_decor") { }

    bool OnGossipHello(Player* /*player*/, GameObject* /*go*/) override
    {
        return true;
    }
};

// A portal: off to its catalogue destination.
class xorwow_guild_hall_portal : public GameObjectScript
{
public:
    xorwow_guild_hall_portal() : GameObjectScript("xorwow_guild_hall_portal") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        CatalogItem item;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = std::find_if(catalog.begin(), catalog.end(), [go](auto const& entry) { return entry.second.goEntry == go->GetEntry(); });
            if (it == catalog.end() || it->second.destMap < 0)
                return true;
            item = it->second;
        }
        if (player->IsInCombat() || player->IsBeingTeleported())
            return true;
        player->TeleportTo(uint32(item.destMap), item.dest.GetPositionX(), item.dest.GetPositionY(), item.dest.GetPositionZ(), item.dest.GetOrientation());
        return true;
    }
};

class xorwow_guild_hall_build_playerscript : public PlayerScript
{
public:
    xorwow_guild_hall_build_playerscript() : PlayerScript("xorwow_guild_hall_build_playerscript",
        { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_LOGOUT }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\t{}", ADDON_PREFIX, TAG);
        if (msg.rfind(prefix, 0) != 0)
            return true;
        std::string_view rest = std::string_view(msg).substr(prefix.size());

        if (rest == "?")
        {
            SendState(player);
            SendCounts(player);
        }
        else if (rest == ";EMBLEM?")
            SendEmblem(player);
        else if (rest == ";ON")
            EnterBuildMode(player);
        else if (rest == ";OFF")
        {
            LeaveBuildMode(player);
            RefreshInteraction(player);
            SendState(player);
        }
        else if (rest.rfind(";AIM;", 0) == 0)
            SetPending(player, rest.substr(5), false);
        else if (rest.rfind(";MOVE;", 0) == 0)
            SetPending(player, rest.substr(6), true);
        else if (rest.rfind(";GRAB;", 0) == 0 || rest.rfind(";FIND;", 0) == 0)
        {
            bool grab = rest[1] == 'G';
            std::optional<PlacedObject> object = IsBuilder(player) ? ObjectByName(player, rest.substr(6)) : std::nullopt;
            if (!object)
                SendError(player, "That is not one of your hall's objects.");
            else if (grab)
            {
                Pending* pending = player->CustomData.GetDefault<Pending>(PENDING_KEY);
                pending->item = 0;
                pending->moveObject = object->id;
                pending->scale = 100;
                pending->rotation = 0;
            }
            else
                SendSelection(player, *object);
        }
        else if (rest.rfind(";TURN;", 0) == 0)
        {
            std::string_view args = rest.substr(6);
            size_t sep = args.find(';');
            std::optional<int32> degrees = sep == std::string_view::npos ? std::nullopt : Acore::StringTo<int32>(args.substr(0, sep));
            if (degrees && *degrees >= -180 && *degrees <= 180)
                Turn(player, *degrees, args.substr(sep + 1));
        }
        else if (rest.rfind(";REFUND;", 0) == 0)
        {
            if (std::optional<uint32> id = Acore::StringTo<uint32>(rest.substr(8)))
                Refund(player, *id);
        }
        else
            return true;   // an answer of ours echoed back, or someone else's
        return false;
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (IsBuilder(player) && !IsInOwnGuildHall(player))
        {
            LeaveBuildMode(player);
            SendState(player);
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        LeaveBuildMode(player);
    }
};

class xorwow_guild_hall_build_mapscript : public AllMapScript
{
public:
    xorwow_guild_hall_build_mapscript() : AllMapScript("xorwow_guild_hall_build_mapscript",
        { ALLMAPHOOK_ON_PLAYER_ENTER_ALL, ALLMAPHOOK_ON_DESTROY_MAP }) { }

    void OnPlayerEnterAll(Map* map, Player* /*player*/) override
    {
        if (IsGuildHallMap(map->GetId()) && map->IsDungeon())
            Populate(map);
    }

    void OnDestroyMap(Map* map) override
    {
        if (!IsGuildHallMap(map->GetId()))
            return;
        std::lock_guard<std::mutex> guard(lock);
        spawned.erase(InstanceKey(map->GetId(), map->GetInstanceId()));
    }
};

class xorwow_guild_hall_build_serverscript : public ServerScript
{
public:
    xorwow_guild_hall_build_serverscript() : ServerScript("xorwow_guild_hall_build_serverscript", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    // Build mode: a right-click on something placed selects it instead of using it.
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!session || !IsUseRequest(packet.GetOpcode()))
            return true;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld() || !IsGuildHallMap(player->GetMapId()) || !IsBuilder(player))
            return true;
        if (packet.size() < 8)
            return true;

        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid guid;
        copy >> guid;
        return !Select(player, guid);
    }
};

class xorwow_guild_hall_build_guildscript : public GuildScript
{
public:
    xorwow_guild_hall_build_guildscript() : GuildScript("xorwow_guild_hall_build_guildscript", { GUILDHOOK_ON_DISBAND }) { }

    // A disbanded guild's hall goes with it (guild ids are reused).
    void OnDisband(Guild* guild) override
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            auto placed = objects.find(guild->GetId());
            if (placed != objects.end())
            {
                for (auto const& [id, object] : placed->second)
                    DespawnEverywhere(object);
                objects.erase(placed);
            }
        }
        CharacterDatabase.Execute("DELETE FROM xorwow_guild_hall_object WHERE guildid = {}", guild->GetId());
    }
};

class xorwow_guild_hall_build_worldscript : public WorldScript
{
public:
    xorwow_guild_hall_build_worldscript() : WorldScript("xorwow_guild_hall_build_worldscript",
        { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }

    void OnStartup() override
    {
        LoadCatalog();
        LoadObjects();
        // the hall's furniture (GO_FLAG_INTERACT_COND, gameobject_template_addon) is clickable for builders only
        GameObject::XorWoWInteractCheck = [](GameObject const* go, Player const* player)
        {
            if (go->GetEntry() <= 260000 || go->GetEntry() >= 261000)
                return false;
            std::lock_guard<std::mutex> guard(builderLock);
            return builders.count(player->GetGUID()) != 0;
        };
    }

    // After the map updates: the placements the spell queued, paid and spawned.
    void OnUpdate(uint32 /*diff*/) override
    {
        std::vector<Placement> queued;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (placements.empty())
                return;
            queued.swap(placements);
        }
        for (Placement const& placement : queued)
            if (Player* player = ObjectAccessor::FindPlayer(placement.player))
                if (player->IsInWorld() && !player->IsBeingTeleported())
                    Place(player, placement.dest);
    }
};

void AddSC_xorwow_guild_hall_build()
{
    RegisterSpellScript(spell_xorwow_guild_hall_placement);
    new xorwow_guild_hall_decor();
    new xorwow_guild_hall_portal();
    new xorwow_guild_hall_build_playerscript();
    new xorwow_guild_hall_build_mapscript();
    new xorwow_guild_hall_build_serverscript();
    new xorwow_guild_hall_build_guildscript();
    new xorwow_guild_hall_build_worldscript();
}
