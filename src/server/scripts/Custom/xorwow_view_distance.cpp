/*
 * XorWoW: how far away a player sees creatures, other players and server-sent objects (chests,
 * mailboxes, doors, banners...), chosen on the XorWoW addon's Graphics page.
 *
 * The client can only draw what the server has told it about, and Visibility.Distance.Continents
 * limits that to 100 yards whatever the graphics settings say. The addon whispers
 * "XorWoW\tVIEW;<yards>" (100 to 400) to its own player (swallowed here); the server forgets it at
 * logout, so the addon sends it again at every login. Player::GetSightRange does the rest, for this
 * player alone: nobody else's range changes, and bots never send it.
 *
 * The map's own range still applies where it is larger, and battlegrounds and arenas are left alone:
 * the core shrinks an arena to 30 yards before the gates open so the teams cannot scout each other.
 *
 * What this script adds is the refresh. Creatures and players entering range tell whoever is within
 * THEIR visibility range, the map's 100 yards, so they would not reach a player looking at them from
 * 300 yards, and the core loads grids only out to MAX_VISIBILITY_DISTANCE. So, every 2 seconds, a
 * player with a longer reach loads the grids it covers and updates what it sees.
 */

#include "Chat.h"
#include "DataMap.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "StringConvert.h"
#include "StringFormat.h"

#include <algorithm>
#include <string>

namespace
{
    constexpr char ADDON_PREFIX[] = "XorWoW";
    constexpr char VIEW_KEY[] = "xorwow-view-distance";
    constexpr uint32 REFRESH_INTERVAL_MS = 2000;
    constexpr uint32 MIN_YARDS = 100;   // the realm's Visibility.Distance.Continents: nothing to do
    constexpr uint32 MAX_YARDS = 400;   // 4 times that, and VISIBILITY_DISTANCE_GIGANTIC in the core

    struct ViewRefresh : public DataMap::Base
    {
        uint32 timer = 0;
        bool active = false;
    };
}

class xorwow_view_distance_playerscript : public PlayerScript
{
public:
    xorwow_view_distance_playerscript() : PlayerScript("xorwow_view_distance_playerscript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_UPDATE }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tVIEW;", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        Optional<uint32> yards = Acore::StringTo<uint32>(msg.substr(prefix.size()));
        if (!yards)
            return false;

        uint32 const clamped = std::clamp(*yards, MIN_YARDS, MAX_YARDS);
        float const distance = clamped > MIN_YARDS ? float(clamped) : 0.0f;
        bool const changed = distance != player->GetViewDistance();
        player->SetViewDistance(distance);

        if (changed || distance > 0.0f)
        {
            // at the next update rather than here: this runs outside the map's update
            ViewRefresh* refresh = player->CustomData.GetDefault<ViewRefresh>(VIEW_KEY);
            refresh->active = true;
            refresh->timer = REFRESH_INTERVAL_MS;
        }
        return false;
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        ViewRefresh* refresh = player->CustomData.Get<ViewRefresh>(VIEW_KEY);
        if (!refresh || !refresh->active)
            return;

        refresh->timer += diff;
        if (refresh->timer < REFRESH_INTERVAL_MS)
            return;
        refresh->timer = 0;

        if (!player->IsInWorld() || player->IsBeingTeleported())
            return;

        float const distance = player->GetViewDistance();
        if (distance <= 0.0f)
        {
            // back to the standard range: one last pass drops what is now too far, then nothing more
            player->UpdateVisibilityForPlayer();
            refresh->active = false;
            return;
        }

        Map* map = player->GetMap();
        if (distance <= map->GetVisibilityRange() || map->IsBattlegroundOrArena())
            return;

        map->LoadGridsInRange(*player, distance);
        player->UpdateVisibilityForPlayer();
    }
};

void AddSC_xorwow_view_distance()
{
    new xorwow_view_distance_playerscript();
}
