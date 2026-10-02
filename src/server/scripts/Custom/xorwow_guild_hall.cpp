/*
 * XorWoW: guild halls.
 *
 * Every guild has a hall of its own: map 725 for the Alliance, 726 for the Horde, both copies of
 * Dalaran Sewers (617) - same geometry, none of the arena's spawns. The maps come from the client
 * patch (Patch-Z.MPQ: Map, MapDifficulty, AreaTable, Light rows, loading screens 255/256) and
 * scripts\build-patch.ps1 (the server's terrain/collision/pathing files under the new ids); the
 * world rows are data/sql/custom/db_world/2026_10_02_0*_xorwow_*.sql.
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
 * teleportation (spell 260001): the Hearthstone's 10 s cast, its own 30 min cooldown, landing at
 * the entrance - the arena's team start point (Alliance: team 1's, Horde: team 2's), clear of
 * anything placed in the hall.
 *
 * Who may edit the hall (prop placement, later) is a per-rank toggle: the guild master's rank
 * always may, every other rank when its row is in characters.xorwow_guild_hall_rank. The client's
 * own rank rights cannot carry it - its Guild Control window maps its checkboxes to the 17 stock
 * rights through a fixed table and sends back only those - so the XorWoW addon adds an "Edit Guild
 * Hall" checkbox to that window and talks to this script over addon whispers to itself:
 *   "XorWoW\tGHRANKS?"               -> "XorWoW\tGHRANKS;<mask>" (bit n = rank n may edit)
 *   "XorWoW\tGHRANKS;<rank>=<0|1>,..." the guild master sets ranks 1..9, same answer.
 * Rank ids are reused: a rank added after one was deleted takes the old id, so its row is dropped
 * when the guild master adds a rank; a disbanded guild's rows go with it.
 */

#include "Chat.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "MapMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <mutex>
#include <optional>
#include <unordered_map>

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

    // The arena's team start points (game_graveyard 1362/1363): clear floor, never in the way.
    Hall const ALLIANCE_HALL = { 725, 4988, { 1218.01f, 764.795f, 14.7297f, 0.0f } };
    Hall const HORDE_HALL    = { 726, 4989, { 1361.76f, 817.337f, 14.8449f, float(M_PI) } };

    Hall const* HallOfMap(uint32 mapId)
    {
        if (mapId == ALLIANCE_HALL.mapId)
            return &ALLIANCE_HALL;
        if (mapId == HORDE_HALL.mapId)
            return &HORDE_HALL;
        return nullptr;
    }

    Hall const& HallOfTeam(TeamId team)
    {
        return team == TEAM_HORDE ? HORDE_HALL : ALLIANCE_HALL;
    }

    // Both are read and written from the map threads too (chat, teleports): under one lock.
    std::mutex lock;
    std::unordered_map<uint64, uint32> hallInstances;   // (map id << 32 | guild id) -> instance id
    std::unordered_map<uint32, uint32> editRanks;       // guild id -> ranks that may edit, bit n = rank n

    uint64 HallKey(uint32 mapId, uint32 guildId)
    {
        return (uint64(mapId) << 32) | guildId;
    }

    // The instance of the player's guild's hall on this map; 0 = none yet (or no guild).
    uint32 GuildInstance(uint32 mapId, Player* player)
    {
        if (!player->GetGuildId())
            return 0;
        std::lock_guard<std::mutex> guard(lock);
        auto it = hallInstances.find(HallKey(mapId, player->GetGuildId()));
        return it == hallInstances.end() ? 0 : it->second;
    }

    void SetGuildInstance(uint32 mapId, Player* player, uint32 instanceId)
    {
        if (!player->GetGuildId())
            return;   // a game master without a guild: a hall of their own, forgotten
        std::lock_guard<std::mutex> guard(lock);
        hallInstances[HallKey(mapId, player->GetGuildId())] = instanceId;
    }

    bool InOwnGuildHall(Player* player)
    {
        return HallOfMap(player->GetMapId()) && player->GetGuildId()
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
        Hall const& hall = HallOfTeam(player->GetTeamId());
        if (!player->GetGuildId() || !MapMgr::ExistMapAndVMap(hall.mapId, hall.entrance.GetPositionX(), hall.entrance.GetPositionY()))
            return std::nullopt;   // no guild, or the hall's map files are missing (build-patch.ps1 copies them)
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
}

// The guild master always may; other ranks by the toggle. For the prop placement to come.
bool CanEditGuildHall(Player* player)
{
    return player->GetGuildId() && (EditMask(player->GetGuildId()) & (1u << player->GetRank()));
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

    // Guild members only, each into their own faction's hall (game masters never get here).
    bool OnPlayerCanEnterMap(Player* player, MapEntry const* entry, InstanceTemplate const* /*instance*/, MapDifficulty const* /*mapDiff*/, bool /*loginCheck*/) override
    {
        Hall const* hall = HallOfMap(entry->MapID);
        if (!hall)
            return true;
        return player->GetGuildId() && hall == &HallOfTeam(player->GetTeamId());
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        Eviction* eviction = player->CustomData.Get<Eviction>(EVICTION_KEY);
        if (!eviction || !eviction->remaining)
            return;

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
            hallInstances.erase(HallKey(ALLIANCE_HALL.mapId, guild->GetId()));
            hallInstances.erase(HallKey(HORDE_HALL.mapId, guild->GetId()));
        }
        CharacterDatabase.Execute("DELETE FROM xorwow_guild_hall_rank WHERE guildid = {}", guild->GetId());
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
        for (Hall const* hall : { &ALLIANCE_HALL, &HORDE_HALL })
        {
            uint32 const mapId = hall->mapId;
            MapMgr::ScriptedInstanceMap scripted;
            scripted.Pick = [mapId](Player* player) { return GuildInstance(mapId, player); };
            scripted.Created = [mapId](Player* player, uint32 instanceId) { SetGuildInstance(mapId, player, instanceId); };
            scripted.AreaId = hall->areaId;
            sMapMgr->SetScriptedInstanceMap(mapId, std::move(scripted));
        }

        std::lock_guard<std::mutex> guard(lock);
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
