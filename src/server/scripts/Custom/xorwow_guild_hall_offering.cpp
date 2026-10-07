/*
 * XorWoW: Golden Offering (spell 260011), the guild hall's gold coin toss.
 *
 * A 1 s cast at a spot on the ground (the targeting circle): gold coins fly there and burst in a
 * golden explosion. Each cast costs 1 gold, deposited into the caster's guild bank (the bank log
 * shows it as the member's deposit).
 *
 * Guild members learn it the first time they enter their own guild hall and keep it; it can only
 * be cast there (the spell's AreaGroup 2629 = the hall areas 4988-4993 greys it out elsewhere, and
 * CheckCast below wants the caster's own hall). Bots never learn it. The spell, its visual and the
 * coin models come from the client patch (client-patch/patch.json, scripts/client-patch/
 * gen_gold_offering.py); spell_dbc and areagroup_dbc from 2026_10_02_00_xorwow_client_patch_dbc.sql.
 *
 * The deposit is queued and made in the world update, like the build mode's payments: the core only
 * touches a guild's bank money from the world thread, never alongside the map updates.
 */

#include "Chat.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "WorldSession.h"

#include <mutex>
#include <vector>

bool IsInOwnGuildHall(Player* player);

namespace
{
    constexpr uint32 OFFERING_SPELL = 260011;
    constexpr uint32 OFFERING_PRICE = 1 * GOLD;

    struct Offering
    {
        ObjectGuid player;
        uint32 guildId;
    };

    std::mutex lock;
    std::vector<Offering> offerings;   // from the spell, for the world update

    std::string Refusal(Player* player)
    {
        if (!IsInOwnGuildHall(player))
            return "You can only do that in your guild hall.";
        Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
        if (!guild)
            return "You can only do that in your guild hall.";
        if (player->GetMoney() < OFFERING_PRICE)
            return "You need 1 gold for a Golden Offering.";
        if (guild->GetTotalBankMoney() > GUILD_BANK_MONEY_LIMIT - OFFERING_PRICE)
            return "Your guild bank cannot hold any more gold.";
        return {};
    }
}

class spell_xorwow_golden_offering : public SpellScript
{
    PrepareSpellScript(spell_xorwow_golden_offering)

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return SPELL_FAILED_DONT_REPORT;
        std::string refusal = Refusal(player);
        if (!refusal.empty())
        {
            ChatHandler(player->GetSession()).SendNotification("{}", refusal);
            return SPELL_FAILED_DONT_REPORT;
        }
        return SPELL_CAST_OK;
    }

    // paid once the cast went off, whatever the coins hit
    void HandleAfterCast()
    {
        if (Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr)
        {
            std::lock_guard<std::mutex> guard(lock);
            offerings.push_back({ player->GetGUID(), player->GetGuildId() });
        }
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_xorwow_golden_offering::CheckCast);
        AfterCast += SpellCastFn(spell_xorwow_golden_offering::HandleAfterCast);
    }
};

// Entering their own guild hall teaches guild members the Golden Offering, once.
class xorwow_guild_hall_offering_playerscript : public PlayerScript
{
public:
    xorwow_guild_hall_offering_playerscript() : PlayerScript("xorwow_guild_hall_offering_playerscript",
        { PLAYERHOOK_ON_MAP_CHANGED }) { }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!player->GetSession()->IsBot() && !player->HasSpell(OFFERING_SPELL) && IsInOwnGuildHall(player))
            player->learnSpell(OFFERING_SPELL);
    }
};

class xorwow_guild_hall_offering_worldscript : public WorldScript
{
public:
    xorwow_guild_hall_offering_worldscript() : WorldScript("xorwow_guild_hall_offering_worldscript",
        { WORLDHOOK_ON_UPDATE }) { }

    // After the map updates: the offerings the spell queued, into the guild banks.
    void OnUpdate(uint32 /*diff*/) override
    {
        std::vector<Offering> queued;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (offerings.empty())
                return;
            queued.swap(offerings);
        }
        for (Offering const& offering : queued)
        {
            Player* player = ObjectAccessor::FindPlayer(offering.player);
            Guild* guild = sGuildMgr->GetGuildById(offering.guildId);
            // spent or left the guild since the cast: nothing to take
            if (!player || !guild || player->GetGuildId() != offering.guildId || player->GetMoney() < OFFERING_PRICE)
                continue;
            guild->HandleMemberDepositMoney(player->GetSession(), OFFERING_PRICE);
        }
    }
};

void AddSC_xorwow_guild_hall_offering()
{
    RegisterSpellScript(spell_xorwow_golden_offering);
    new xorwow_guild_hall_offering_playerscript();
    new xorwow_guild_hall_offering_worldscript();
}
