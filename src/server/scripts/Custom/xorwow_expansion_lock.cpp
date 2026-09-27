/*
 * XorWoW: Outland and Northrend are closed while the realm is capped at level 60.
 *
 * The rest of the lock is data (data/sql/custom/db_world/2026_09_27_00_xorwow_expansion_lock.sql):
 * every Burning Crusade and Wrath dungeon and raid is in `disables`, which takes them out of the
 * dungeon finder and closes their entrances, and the four boats/zeppelins to Northrend are gone.
 * This script covers what data cannot:
 *
 *  - any teleport into Northrend (map 571) or into the Outland zones of map 530 is refused: the
 *    Dark Portal, summons, a bot following its master. Map 530 also holds Eversong, Ghostlands,
 *    Silvermoon, Azuremyst, Bloodmyst and the Exodar, so it is closed zone by zone, not as a map.
 *  - a character that logs in or wanders (zone change) into a closed place is sent to their
 *    hearthstone point, or to Stormwind/Orgrimmar when that is closed too (their hearthstone is
 *    rebound there). Random bots that were above 60 are the ones this catches.
 *
 * GMs with .gm on are exempt. To reopen, remove this script from custom_script_loader.cpp and run
 * the undo block at the end of the SQL file.
 */

#include "AreaDefines.h"
#include "Chat.h"
#include "DBCStores.h"
#include "MapMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <unordered_set>

namespace
{
    // Zones of map 530 that are Outland (plus the level 70 Isle of Quel'Danas). The Blood Elf and
    // Draenei starting zones and the seas around them stay open.
    std::unordered_set<uint32> const ClosedOutlandZones =
    {
        3483,   // Hellfire Peninsula
        3518,   // Nagrand
        3519,   // Terokkar Forest
        3520,   // Shadowmoon Valley
        3521,   // Zangarmarsh
        3522,   // Blade's Edge Mountains
        3523,   // Netherstorm
        3540,   // Twisting Nether
        3703,   // Shattrath City
        3917,   // Auchindoun
        4080,   // Isle of Quel'Danas
    };

    constexpr char const* ClosedMessage = "Outland and Northrend are closed while the level cap is 60.";

    bool IsClosed(uint32 mapId, uint32 zoneId)
    {
        if (mapId == MAP_NORTHREND)
            return true;
        if (mapId == MAP_OUTLAND)
            return ClosedOutlandZones.count(zoneId) != 0;
        // Inside a Burning Crusade or Wrath dungeon/raid (closed by `disables` for new entries)
        if (MapEntry const* map = sMapStore.LookupEntry(mapId))
            return map->IsDungeon() && map->Expansion() >= 1;
        return false;
    }

    uint32 ZoneOfArea(uint32 areaId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        if (!area)
            return 0;
        return area->zone ? area->zone : area->ID;
    }

    // Hearthstone point, or the faction capital when that is closed too (and rebind there).
    void SendOutOfClosedPlace(Player* player)
    {
        if (player->IsBeingTeleported())
            return;

        WorldLocation home(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ,
            player->GetOrientation());
        std::string where = "your hearthstone point";

        if (IsClosed(player->m_homebindMapId, ZoneOfArea(player->m_homebindAreaId)))
        {
            if (player->GetTeamId() == TEAM_ALLIANCE)
            {
                home = WorldLocation(MAP_EASTERN_KINGDOMS, -8833.38f, 628.628f, 94.0066f, 1.06535f);
                player->SetHomebind(home, 1519);    // Stormwind City
                where = "Stormwind, and your hearthstone is now set there";
            }
            else
            {
                home = WorldLocation(MAP_KALIMDOR, 1629.85f, -4373.64f, 31.5573f, 3.69762f);
                player->SetHomebind(home, 1637);    // Orgrimmar
                where = "Orgrimmar, and your hearthstone is now set there";
            }
        }

        if (player->TeleportTo(home))
            ChatHandler(player->GetSession()).PSendSysMessage("{} You have been sent to {}.", ClosedMessage, where);
    }
}

class xorwow_expansion_lock_playerscript : public PlayerScript
{
public:
    xorwow_expansion_lock_playerscript() : PlayerScript("xorwow_expansion_lock_playerscript",
        { PLAYERHOOK_ON_BEFORE_TELEPORT, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE_ZONE }) { }

    bool OnPlayerBeforeTeleport(Player* player, uint32 mapId, float x, float y, float z, float /*orientation*/,
        uint32 /*options*/, Unit* /*target*/) override
    {
        if (player->IsGameMaster())
            return true;

        if (mapId != MAP_NORTHREND && mapId != MAP_OUTLAND)
            return true;

        if (!IsClosed(mapId, sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, x, y, z)))
            return true;

        player->GetSession()->SendAreaTriggerMessage(ClosedMessage);
        return false;
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player->IsGameMaster() && IsClosed(player->GetMapId(), player->GetZoneId()))
            SendOutOfClosedPlace(player);
    }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
    {
        if (!player->IsGameMaster() && IsClosed(player->GetMapId(), newZone))
            SendOutOfClosedPlace(player);
    }
};

void AddSC_xorwow_expansion_lock()
{
    new xorwow_expansion_lock_playerscript();
}
