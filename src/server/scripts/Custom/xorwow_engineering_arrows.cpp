/*
 * XorWoW: engineers craft arrows as well as bullets, from the same recipes and the same materials.
 *
 * Engineering only has bullet recipes, and a new recipe would need new spells in the client's own
 * Spell.dbc/SkillLineAbility.dbc (the tradeskill window is drawn from them), which the realm does not
 * patch. So the existing bullet recipes make either: the recipe's create-item effect is replaced by
 * the same creation (specialization procs, skill-ups, bag checks: Spell::DoCreateItem) of the arrow
 * that matches the bullet. Which one comes out is the crafter's choice, ".ammo":
 *   auto     arrows while a bow or crossbow is equipped, bullets otherwise (the default)
 *   arrows   always arrows
 *   bullets  always bullets (the stock recipes)
 * The choice lasts for the session; the XorWoW client addon remembers it per character, sends it
 * again at login (an addon whisper to itself, "XorWoW\tAMMO;<choice>", answered and swallowed here)
 * and shows the arrow in the tradeskill window's recipe details ("XorWoW\tAMMO?" asks what the
 * recipes make right now). Every answer is "AMMO;<choice>;<arrows|bullets>".
 *
 * Two arrows had no twin and are unused, deprecated item entries that the client already knows as
 * arrows (data/sql/custom/db_world/2026_09_23_00_xorwow_engineering_arrows.sql, which also binds
 * the recipes to this script): Mithril Gyro-Arrow and Fel Iron Arrow.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "DataMap.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <string>

using namespace Acore::ChatCommands;

namespace
{
    // The engineering bullet recipes and the arrow each one makes instead: the same damage per
    // second where an arrow exists, the nearest vendor arrow for the three low tiers.
    struct Fletching
    {
        uint32 spellId;
        uint32 arrowId;
    };

    constexpr Fletching FLETCHINGS[] =
    {
        {  3920,  2512 },   // Crafted Light Shot       -> Rough Arrow
        {  3930,  2515 },   // Crafted Heavy Shot       -> Sharp Arrow
        {  3947,  3030 },   // Crafted Solid Shot       -> Razor Arrow
        { 12596, 11285 },   // Hi-Impact Mithril Slugs  -> Jagged Arrow
        { 12621,  3031 },   // Mithril Gyro-Shot        -> Mithril Gyro-Arrow (was "Depricated Razor Arrow")
        { 19800, 18042 },   // Thorium Shells           -> Thorium Headed Arrow
        { 30346,  3029 },   // Fel Iron Shells          -> Fel Iron Arrow (was "Depricated Whipwood Arrow")
    };

    uint32 ArrowFor(uint32 spellId)
    {
        for (Fletching const& fletching : FLETCHINGS)
            if (fletching.spellId == spellId)
                return fletching.arrowId;
        return 0;
    }

    enum class AmmoChoice : uint8
    {
        Auto,
        Arrows,
        Bullets
    };

    struct AmmoChoiceData : public DataMap::Base
    {
        AmmoChoice choice = AmmoChoice::Auto;
    };

    constexpr char CHOICE_KEY[] = "xorwow-ammo";
    constexpr char ADDON_PREFIX[] = "XorWoW";

    AmmoChoice GetChoice(Player* player)
    {
        if (AmmoChoiceData* data = player->CustomData.Get<AmmoChoiceData>(CHOICE_KEY))
            return data->choice;
        return AmmoChoice::Auto;
    }

    char const* ChoiceName(AmmoChoice choice)
    {
        switch (choice)
        {
            case AmmoChoice::Arrows:  return "arrows";
            case AmmoChoice::Bullets: return "bullets";
            default:                  return "auto";
        }
    }

    bool ParseChoice(std::string const& text, AmmoChoice& choice)
    {
        if (text == "auto")
            choice = AmmoChoice::Auto;
        else if (text == "arrows" || text == "arrow" || text == "bow")
            choice = AmmoChoice::Arrows;
        else if (text == "bullets" || text == "bullet" || text == "gun")
            choice = AmmoChoice::Bullets;
        else
            return false;
        return true;
    }

    // Whatever sits in the ranged slot, broken or not: it is about what the crafter holds.
    bool HoldsBowOrCrossbow(Player* player)
    {
        Item* ranged = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (!ranged)
            return false;
        ItemTemplate const* proto = ranged->GetTemplate();
        return proto->Class == ITEM_CLASS_WEAPON
            && (proto->SubClass == ITEM_SUBCLASS_WEAPON_BOW || proto->SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW);
    }

    bool MakesArrows(Player* player)
    {
        switch (GetChoice(player))
        {
            case AmmoChoice::Arrows:  return true;
            case AmmoChoice::Bullets: return false;
            default:                  return HoldsBowOrCrossbow(player);
        }
    }

    void SendToAddon(Player* player)
    {
        std::string body = Acore::StringFormat("AMMO;{};{}", ChoiceName(GetChoice(player)), MakesArrows(player) ? "arrows" : "bullets");
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Acore::StringFormat("{}\t{}", ADDON_PREFIX, body));
        player->SendDirectMessage(&data);
    }

    std::string Describe(Player* player)
    {
        switch (GetChoice(player))
        {
            case AmmoChoice::Arrows:  return "arrows, whatever you hold";
            case AmmoChoice::Bullets: return "bullets, whatever you hold";
            default:
                return Acore::StringFormat("auto - arrows with a bow or crossbow equipped, bullets otherwise (now {})",
                    HoldsBowOrCrossbow(player) ? "arrows" : "bullets");
        }
    }
}

// Bound to the bullet recipes in spell_script_names.
class spell_xorwow_engineering_arrows : public SpellScript
{
    PrepareSpellScript(spell_xorwow_engineering_arrows)

    bool Validate(SpellInfo const* spellInfo) override
    {
        uint32 arrowId = ArrowFor(spellInfo->Id);
        return arrowId && sObjectMgr->GetItemTemplate(arrowId);
    }

    void HandleCreateItem(SpellEffIndex effIndex)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || !MakesArrows(player))
            return;   // the recipe's own bullets

        uint32 arrowId = ArrowFor(GetSpellInfo()->Id);
        PreventHitDefaultEffect(effIndex);
        GetSpell()->DoCreateItem(effIndex, arrowId);
        GetSpell()->ExecuteLogEffectCreateItem(effIndex, arrowId);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_xorwow_engineering_arrows::HandleCreateItem, EFFECT_0, SPELL_EFFECT_CREATE_ITEM);
    }
};

class xorwow_engineering_arrows_commandscript : public CommandScript
{
public:
    xorwow_engineering_arrows_commandscript() : CommandScript("xorwow_engineering_arrows_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "ammo", HandleAmmoCommand, SEC_PLAYER, Console::No }
        };
        return commandTable;
    }

    // ".ammo" shows the choice, ".ammo auto|arrows|bullets" sets it.
    static bool HandleAmmoCommand(ChatHandler* handler, Optional<std::string> argument)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (argument)
        {
            AmmoChoice choice;
            if (!ParseChoice(*argument, choice))
            {
                handler->SendSysMessage("Usage: .ammo auto | arrows | bullets");
                return true;
            }
            player->CustomData.GetDefault<AmmoChoiceData>(CHOICE_KEY)->choice = choice;
            SendToAddon(player);   // so the addon remembers a choice typed by hand
        }

        handler->PSendSysMessage("Engineering bullet recipes make: {}.", Describe(player));
        return true;
    }
};

class xorwow_engineering_arrows_playerscript : public PlayerScript
{
public:
    xorwow_engineering_arrows_playerscript() : PlayerScript("xorwow_engineering_arrows_playerscript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    // The addon's "AMMO;<choice>" (sets it without a chat line) and "AMMO?" (asks), whispered to
    // itself; both answered with the current state and swallowed.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || type != CHAT_MSG_WHISPER || receiver != player)
            return true;

        std::string const prefix = Acore::StringFormat("{}\tAMMO", ADDON_PREFIX);
        if (msg.rfind(prefix, 0) != 0)
            return true;

        std::string rest = msg.substr(prefix.size());
        AmmoChoice choice;
        if (rest.size() > 1 && rest[0] == ';' && ParseChoice(rest.substr(1), choice))
            player->CustomData.GetDefault<AmmoChoiceData>(CHOICE_KEY)->choice = choice;
        else if (rest != "?")
            return true;

        SendToAddon(player);
        return false;
    }
};

void AddSC_xorwow_engineering_arrows()
{
    RegisterSpellScript(spell_xorwow_engineering_arrows);
    new xorwow_engineering_arrows_commandscript();
    new xorwow_engineering_arrows_playerscript();
}
