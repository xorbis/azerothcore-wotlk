/*
 * XorWoW: guild halls.
 *
 * Every guild has a hall of its own, in the building its guild master picked (HALLS below, each a
 * pair of maps, Alliance and Horde - same geometry as the original, none of its spawns):
 *   0 Dalaran Sewers (617) as 725/726, the default
 *   1 Nagrand Arena (559) as 727/728
 *   2 Violet Hold (608) as 729/730
 * The maps come from the client patch (Patch-Z.MPQ: Map, MapDifficulty, AreaTable, Light rows; every
 * hall shows the guild hall loading screens 255/256) and scripts\build-patch.ps1 (the server's
 * terrain/collision/pathing files under the new ids); the world rows are
 * data/sql/custom/db_world/2026_10_02_0*_xorwow_*.sql and 2026_10_05_00_xorwow_guild_hall_choices.sql.
 * The pick is characters.xorwow_guild_hall_choice (no row = Dalaran Sewers); a change sells what
 * the old hall held (xorwow_guild_hall_build.cpp) and moves the members inside to the new one.
 *
 * One instance per guild, whoever is grouped with whom: the core asks this script which instance
 * a player goes into (MapMgr::ScriptedInstanceMap, a small XorWoW patch in the core) instead of
 * following instance binds, and exempts these maps from the hourly instance limit, instance
 * resets, the lock warning and the party-only rules. An instance lives while it is in use and
 * unloads when it has been empty for Instance.UnloadDelay; the next visit makes a new one.
 * Only members of a guild get in - players, summoned bots, logins alike (a bot outside the guild
 * cannot be summoned in); a member removed from the guild while inside is sent to their
 * hearthstone location after 60 seconds, as a player removed from a dungeon group is.
 *
 * Inside, the city rules: the whole map is one area, "Guild Hall" (4988/4989), with Stormwind's
 * flags - resting, no duels, no free-for-all, the faction's own territory. A ghost released inside
 * appears at the entrance (graveyards 1721/1722).
 *
 * The Guildstone (item 260001, 10 gold at every guild tabard vendor) casts Guild Hall
 * teleportation (spell 260001): the Hearthstone's 10 s cast, its own 15 min cooldown, landing at
 * the entrance - the arena's team start point (Alliance: team 1's, Horde: team 2's), clear of
 * anything placed in the hall.
 *
 * Who may edit the hall (build mode, xorwow_guild_hall_build.cpp) is a per-rank toggle: the guild master's rank
 * always may, every other rank when its row is in characters.xorwow_guild_hall_rank. The client's
 * own rank rights cannot carry it - its Guild Control window maps its checkboxes to the 17 stock
 * rights through a fixed table and sends back only those - so the XorWoW addon adds an "Edit Guild
 * Hall" checkbox to that window and talks to this script over addon whispers to itself:
 *   "XorWoW\tGHRANKS?"               -> "XorWoW\tGHRANKS;<mask>" (bit n = rank n may edit)
 *   "XorWoW\tGHRANKS;<rank>=<0|1>,..." the guild master sets ranks 1..9, same answer.
 * Rank ids are reused: a rank added after one was deleted takes the old id, so its row is dropped
 * when the guild master adds a rank; a disbanded guild's rows go with it.
 */

#include "CharacterCache.h"
#include "Chat.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <iterator>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr char RANKS_TAG[] = "GHRANKS";
    constexpr char EVICTION_KEY[] = "xorwow-guild-hall-eviction";
    constexpr uint32 EVICTION_DELAY_MS = 60 * IN_MILLISECONDS;

    struct Hall
    {
        uint32 mapId;
        uint32 areaId;
        Position entrance;
    };

    struct HallChoice
    {
        char const* name;
        Hall alliance;
        Hall horde;
    };

    // The entrances are clear floor, never in the way: the arenas' team start points (Dalaran:
    // game_graveyard 1362/1363; Nagrand's facing the middle of the arena), the dungeons' own
    // entrances (their areatrigger_teleport targets). The order is the addon's (GuildHallBuild.lua)
    // and the saved choice's: append only.
    HallChoice const HALLS[] =
    {
        { "Dalaran Sewers",     { 725, 4988, { 1218.01f, 764.795f, 14.7297f, 0.0f } },   { 726, 4989, { 1361.76f, 817.337f, 14.8449f, float(M_PI) } } },
        { "Nagrand Arena",      { 727, 4990, { 4027.6f, 2972.78f, 12.0723f, 5.196f } },  { 728, 4991, { 4085.45f, 2866.83f, 12.4005f, 2.087f } } },
        { "Violet Hold",        { 729, 4992, { 1808.82f, 803.93f, 44.364f, 6.282f } },   { 730, 4993, { 1808.82f, 803.93f, 44.364f, 6.282f } } },
    };
    constexpr uint8 HALL_COUNT = uint8(std::size(HALLS));

    Hall const* HallOfMap(uint32 mapId)
    {
        for (HallChoice const& choice : HALLS)
        {
            if (mapId == choice.alliance.mapId)
                return &choice.alliance;
            if (mapId == choice.horde.mapId)
                return &choice.horde;
        }
        return nullptr;
    }

    // All are read and written from the map threads too (chat, teleports): under one lock.
    std::mutex lock;
    std::unordered_map<uint64, uint32> hallInstances;   // (map id << 32 | guild id) -> instance id
    std::unordered_map<uint32, uint32> editRanks;       // guild id -> ranks that may edit, bit n = rank n
    std::unordered_map<uint32, uint8> choices;          // guild id -> its HALLS index; none = 0

    uint8 ChoiceOf(uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = choices.find(guildId);
        return it == choices.end() ? 0 : it->second;
    }

    // The guild's hall for this faction (a guildless player: the default one).
    Hall const& HallOf(uint32 guildId, TeamId team)
    {
        HallChoice const& choice = HALLS[ChoiceOf(guildId)];
        return team == TEAM_HORDE ? choice.horde : choice.alliance;
    }

    uint64 HallKey(uint32 mapId, uint32 guildId)
    {
        return (uint64(mapId) << 32) | guildId;
    }

    // The player's guild. At login the core loads the map before it sets the guild on the
    // character, so a player logging in inside a hall would look guildless (and get a hall of
    // their own): the character cache knows the guild from the start and follows every change.
    uint32 GuildIdOf(Player* player)
    {
        if (uint32 guildId = player->GetGuildId())
            return guildId;
        return sCharacterCache->GetCharacterGuildIdByGuid(player->GetGUID());
    }

    // The instance of the player's guild's hall on this map; 0 = none yet (or no guild).
    uint32 GuildInstance(uint32 mapId, Player* player)
    {
        uint32 guildId = GuildIdOf(player);
        if (!guildId)
            return 0;
        std::lock_guard<std::mutex> guard(lock);
        auto it = hallInstances.find(HallKey(mapId, guildId));
        return it == hallInstances.end() ? 0 : it->second;
    }

    void SetGuildInstance(uint32 mapId, Player* player, uint32 instanceId)
    {
        uint32 guildId = GuildIdOf(player);
        if (!guildId)
            return;   // a game master without a guild: a hall of their own, forgotten
        std::lock_guard<std::mutex> guard(lock);
        hallInstances[HallKey(mapId, guildId)] = instanceId;
    }

    // In the guild's instance of the hall it uses now (not of one it moved out of).
    bool InOwnGuildHall(Player* player)
    {
        uint32 guildId = GuildIdOf(player);
        return HallOfMap(player->GetMapId()) && guildId
            && HallOf(guildId, player->GetTeamId()).mapId == player->GetMapId()
            && GuildInstance(player->GetMapId(), player) == player->GetInstanceId();
    }

    uint32 EditMask(uint32 guildId)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = editRanks.find(guildId);
        return (it == editRanks.end() ? 0 : it->second) | 1;
    }

    void SetRankMayEdit(uint32 guildId, uint8 rank, bool allowed)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            uint32& mask = editRanks[guildId];
            mask = allowed ? (mask | (1u << rank)) : (mask & ~(1u << rank));
        }
        if (allowed)
            CharacterDatabase.Execute("REPLACE INTO xorwow_guild_hall_rank (guildid, rid) VALUES ({}, {})", guildId, rank);
        else
            CharacterDatabase.Execute("DELETE FROM xorwow_guild_hall_rank WHERE guildid = {} AND rid = {}", guildId, rank);
    }

    // Where the Guildstone sends the player: their guild's hall, at its entrance.
    std::optional<WorldLocation> GuildHallEntrance(Player* player)
    {
        uint32 guildId = GuildIdOf(player);
        if (!guildId)
            return std::nullopt;
        Hall const& hall = HallOf(guildId, player->GetTeamId());
        if (!MapMgr::ExistMapAndVMap(hall.mapId, hall.entrance.GetPositionX(), hall.entrance.GetPositionY()))
            return std::nullopt;   // the hall's map files are missing (build-patch.ps1 copies them)
        return WorldLocation(hall.mapId, hall.entrance);
    }

    void SendToAddon(Player* player, std::string const& body)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\t{}", ADDON_PREFIX, body));
        player->SendDirectMessage(&data);
    }

    void SendRanks(Player* player)
    {
        SendToAddon(player, Acore::StringFormat("{};{}", RANKS_TAG, player->GetGuildId() ? EditMask(player->GetGuildId()) : 0));
    }

    // "1=1,3=0": the guild master's changes; anything malformed or out of range is ignored.
    void ApplyRankChanges(Player* player, std::string_view changes)
    {
        Guild* guild = player->GetGuild();
        if (!guild || guild->GetLeaderGUID() != player->GetGUID())
            return;

        for (std::string_view change : Acore::Tokenize(changes, ',', false))
        {
            size_t eq = change.find('=');
            if (eq == std::string_view::npos || eq + 2 != change.size())
                continue;
            std::optional<uint32> rank = Acore::StringTo<uint32>(change.substr(0, eq));
            char value = change[eq + 1];
            if (!rank || *rank == 0 || *rank >= guild->GetRankCount() || (value != '0' && value != '1'))
                continue;
            SetRankMayEdit(guild->GetId(), uint8(*rank), value == '1');
        }
    }

    struct Eviction : public DataMap::Base
    {
        uint32 remaining = 0;   // ms, 0 = none pending
    };

    void StartEviction(Player* player)
    {
        Eviction* eviction = player->CustomData.GetDefault<Eviction>(EVICTION_KEY);
        if (eviction->remaining)
            return;
        eviction->remaining = EVICTION_DELAY_MS;
        ChatHandler(player->GetSession()).SendNotification("You are no longer a member of this guild. You will be teleported out in 60 seconds.");
        ChatHandler(player->GetSession()).SendSysMessage("You are no longer a member of this guild. You will be teleported out of the guild hall in 60 seconds.");
    }

    // One of this faction's halls, whichever building.
    bool IsTeamHall(Hall const* hall, TeamId team)
    {
        for (HallChoice const& choice : HALLS)
            if (hall == (team == TEAM_HORDE ? &choice.horde : &choice.alliance))
                return true;
        return false;
    }

    // Someone left in a hall the guild moved out of (inside at the change, or logged in there
    // later): into the hall it uses now. Not a member any more: home.
    void MoveToCurrentHall(Player* player)
    {
        if (player->IsBeingTeleported())
            return;
        if (!player->IsAlive())
            player->ResurrectPlayer(0.5f);
        if (std::optional<WorldLocation> entrance = GuildHallEntrance(player))
            player->TeleportTo(*entrance);
        else
            player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
    }
}

// The guild master always may; other ranks by the toggle.
bool CanEditGuildHall(Player* player)
{
    return player->GetGuildId() && (EditMask(player->GetGuildId()) & (1u << player->GetRank()));
}

// For the build mode (xorwow_guild_hall_build.cpp).
bool IsGuildHallMap(uint32 mapId)
{
    return HallOfMap(mapId) != nullptr;
}

bool IsInOwnGuildHall(Player* player)
{
    return InOwnGuildHall(player);
}

// The guild whose hall this instance is; 0 = none (a guildless game master's).
uint32 GuildOfHallInstance(uint32 mapId, uint32 instanceId)
{
    std::lock_guard<std::mutex> guard(lock);
    for (auto const& [key, instance] : hallInstances)
        if (instance == instanceId && uint32(key >> 32) == mapId)
            return uint32(key & 0xFFFFFFFF);
    return 0;
}

// Where the Guildstone lands: kept clear of anything placed.
bool NearGuildHallEntrance(uint32 mapId, Position const& pos, float distance)
{
    Hall const* hall = HallOfMap(mapId);
    return hall && hall->entrance.GetExactDist2d(&pos) < distance;
}

// The halls a guild master can pick (the build panel's "Change hall").
uint8 GuildHallCount()
{
    return HALL_COUNT;
}

uint8 GuildHallChoice(uint32 guildId)
{
    return ChoiceOf(guildId);
}

std::string GuildHallName(uint8 hall)
{
    return hall < HALL_COUNT ? HALLS[hall].name : "";
}

// The guild moves to another hall - one at a time, the old one closes to it. Its objects are sold
// first (xorwow_guild_hall_build.cpp); everyone in its old hall instance goes to the new hall's
// entrance. World thread, no map update running.
void ChangeGuildHall(Guild* guild, uint8 hall)
{
    uint32 const guildId = guild->GetId();
    uint8 const old = ChoiceOf(guildId);
    if (hall >= HALL_COUNT || hall == old)
        return;

    std::vector<ObjectGuid> inside;
    for (Hall const* oldHall : { &HALLS[old].alliance, &HALLS[old].horde })
    {
        uint32 instanceId;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = hallInstances.find(HallKey(oldHall->mapId, guildId));
            if (it == hallInstances.end())
                continue;
            instanceId = it->second;
        }
        if (Map* map = sMapMgr->FindMap(oldHall->mapId, instanceId))
            map->DoForAllPlayers([&inside](Player* player) { inside.push_back(player->GetGUID()); });
    }

    {
        std::lock_guard<std::mutex> guard(lock);
        if (hall)
            choices[guildId] = hall;
        else
            choices.erase(guildId);
    }
    if (hall)
        CharacterDatabase.Execute("REPLACE INTO xorwow_guild_hall_choice (guildid, hall) VALUES ({}, {})", guildId, hall);
    else
        CharacterDatabase.Execute("DELETE FROM xorwow_guild_hall_choice WHERE guildid = {}", guildId);

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr,
        Acore::StringFormat("Your guild hall is now in {}.", HALLS[hall].name));
    guild->BroadcastPacket(&data);

    for (ObjectGuid const& guid : inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            if (!player->IsGameMaster())
                MoveToCurrentHall(player);
}

// Guild Hall teleportation (260001), the Guildstone's spell.
class spell_xorwow_guild_hall_teleport : public SpellScript
{
    PrepareSpellScript(spell_xorwow_guild_hall_teleport)

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return SPELL_FAILED_DONT_REPORT;
        if (!player->GetGuildId())
        {
            ChatHandler(player->GetSession()).SendNotification("You are not in a guild.");
            return SPELL_FAILED_DONT_REPORT;
        }
        if (player->GetMap()->IsBattlegroundOrArena())
            return SPELL_FAILED_NOT_HERE;
        if (!GuildHallEntrance(player))
        {
            ChatHandler(player->GetSession()).SendNotification("Your guild hall is not ready yet.");
            return SPELL_FAILED_DONT_REPORT;
        }
        return SPELL_CAST_OK;
    }

    void HandleTeleport(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return;
        if (std::optional<WorldLocation> entrance = GuildHallEntrance(player))
            player->TeleportTo(*entrance);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_xorwow_guild_hall_teleport::CheckCast);
        OnEffectHit += SpellEffectFn(spell_xorwow_guild_hall_teleport::HandleTeleport, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class xorwow_guild_hall_playerscript : public PlayerScript
{
public:
    xorwow_guild_hall_playerscript() : PlayerScript("xorwow_guild_hall_playerscript",
        { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_CAN_ENTER_MAP, PLAYERHOOK_ON_UPDATE }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\t{}", ADDON_PREFIX, RANKS_TAG);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        std::string_view rest = std::string_view(msg).substr(prefix.size());
        if (rest.size() > 1 && rest[0] == ';')
            ApplyRankChanges(player, rest.substr(1));
        else if (rest != "?")
            return true;
        SendRanks(player);
        return false;
    }

    // Guild members only, each into their own faction's hall, the one their guild uses (game
    // masters never get here). A login in a hall the guild has left is let in: the next update
    // moves them to the new one rather than home.
    bool OnPlayerCanEnterMap(Player* player, MapEntry const* entry, InstanceTemplate const* /*instance*/, MapDifficulty const* /*mapDiff*/, bool loginCheck) override
    {
        Hall const* hall = HallOfMap(entry->MapID);
        if (!hall)
            return true;
        uint32 guildId = GuildIdOf(player);
        if (!guildId)
            return false;
        if (loginCheck)
            return IsTeamHall(hall, player->GetTeamId());
        return hall == &HallOf(guildId, player->GetTeamId());
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        Eviction* eviction = player->CustomData.Get<Eviction>(EVICTION_KEY);
        if (!eviction || !eviction->remaining)
        {
            // in a hall the guild moved out of: into the new one
            uint32 guildId;
            if (HallOfMap(player->GetMapId()) && player->IsInWorld() && !player->IsBeingTeleported() && !player->IsGameMaster()
                && (guildId = GuildIdOf(player)) && HallOf(guildId, player->GetTeamId()).mapId != player->GetMapId())
                MoveToCurrentHall(player);
            return;
        }

        if (!HallOfMap(player->GetMapId()) || InOwnGuildHall(player))
        {
            eviction->remaining = 0;   // left already, or back in the guild in time
            return;
        }
        if (eviction->remaining > diff)
        {
            uint32 before = eviction->remaining;
            eviction->remaining -= diff;
            if (before > 10 * IN_MILLISECONDS && eviction->remaining <= 10 * IN_MILLISECONDS)
                ChatHandler(player->GetSession()).SendNotification("You will be teleported out of the guild hall in 10 seconds.");
            return;
        }
        eviction->remaining = 0;
        if (player->IsBeingTeleported())
            return;
        if (!player->IsAlive())
            player->ResurrectPlayer(0.5f);
        player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
    }
};

class xorwow_guild_hall_guildscript : public GuildScript
{
public:
    xorwow_guild_hall_guildscript() : GuildScript("xorwow_guild_hall_guildscript", { GUILDHOOK_ON_REMOVE_MEMBER, GUILDHOOK_ON_DISBAND }) { }

    // Left, kicked or disbanded while inside the hall: out in 60 seconds.
    void OnRemoveMember(Guild* /*guild*/, Player* player, bool /*isDisbanding*/, bool /*isKicked*/) override
    {
        if (player && HallOfMap(player->GetMapId()) && !player->IsGameMaster())
            StartEviction(player);
    }

    // Guild ids are reused, so a disbanded guild's toggles and hall must not wait for the next one.
    void OnDisband(Guild* guild) override
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            editRanks.erase(guild->GetId());
            choices.erase(guild->GetId());
            for (HallChoice const& choice : HALLS)
            {
                hallInstances.erase(HallKey(choice.alliance.mapId, guild->GetId()));
                hallInstances.erase(HallKey(choice.horde.mapId, guild->GetId()));
            }
        }
        CharacterDatabase.Execute("DELETE FROM xorwow_guild_hall_rank WHERE guildid = {}", guild->GetId());
        CharacterDatabase.Execute("DELETE FROM xorwow_guild_hall_choice WHERE guildid = {}", guild->GetId());
    }
};

class xorwow_guild_hall_serverscript : public ServerScript
{
public:
    xorwow_guild_hall_serverscript() : ServerScript("xorwow_guild_hall_serverscript", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    // A new rank takes the next id, which may be a deleted rank's: it starts without the toggle.
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() != CMSG_GUILD_ADD_RANK || !session)
            return true;
        Player* player = session->GetPlayer();
        Guild* guild = player ? player->GetGuild() : nullptr;
        if (guild && guild->GetLeaderGUID() == player->GetGUID() && guild->GetRankCount() < GUILD_RANKS_MAX_COUNT)
            SetRankMayEdit(guild->GetId(), uint8(guild->GetRankCount()), false);
        return true;
    }
};

class xorwow_guild_hall_worldscript : public WorldScript
{
public:
    xorwow_guild_hall_worldscript() : WorldScript("xorwow_guild_hall_worldscript", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        for (HallChoice const& choice : HALLS)
        {
            for (Hall const* hall : { &choice.alliance, &choice.horde })
            {
                uint32 const mapId = hall->mapId;
                MapMgr::ScriptedInstanceMap scripted;
                scripted.Pick = [mapId](Player* player) { return GuildInstance(mapId, player); };
                scripted.Created = [mapId](Player* player, uint32 instanceId) { SetGuildInstance(mapId, player, instanceId); };
                scripted.AreaId = hall->areaId;
                sMapMgr->SetScriptedInstanceMap(mapId, std::move(scripted));
            }
        }

        std::lock_guard<std::mutex> guard(lock);
        choices.clear();
        if (QueryResult result = CharacterDatabase.Query("SELECT guildid, hall FROM xorwow_guild_hall_choice"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 hall = fields[1].Get<uint8>();
                if (hall && hall < HALL_COUNT)
                    choices[fields[0].Get<uint32>()] = hall;
            } while (result->NextRow());
        }
        editRanks.clear();
        if (QueryResult result = CharacterDatabase.Query("SELECT guildid, rid FROM xorwow_guild_hall_rank"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 rank = fields[1].Get<uint8>();
                if (rank < GUILD_RANKS_MAX_COUNT)
                    editRanks[fields[0].Get<uint32>()] |= 1u << rank;
            } while (result->NextRow());
        }
    }
};

void AddSC_xorwow_guild_hall()
{
    RegisterSpellScript(spell_xorwow_guild_hall_teleport);
    new xorwow_guild_hall_playerscript();
    new xorwow_guild_hall_guildscript();
    new xorwow_guild_hall_serverscript();
    new xorwow_guild_hall_worldscript();
}
