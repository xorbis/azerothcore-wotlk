/*
 * XorWoW: ".stable buyslot" - buy the next pet stable slot at the realm's price.
 *
 * The realm charges half the stock price for stable slots (stableslotprices_dbc, see
 * data/sql/custom/db_world/2026_09_23_01_xorwow_stable_slot_prices.sql), but the stock client
 * takes the price from its own StableSlotPrices.dbc and may refuse to send the purchase while the
 * hunter holds less gold than that, as it does for guild bank tabs (xorwow_guild_bank_tab.cpp).
 * This command is the same purchase through the chat-command channel, which the client does not
 * pre-check: it finds the stable master the hunter is standing at and runs the core's own
 * CMSG_BUY_STABLE_SLOT handler for it (same rules, the realm's price), then refreshes the stable
 * window. The XorWoW client addon routes the stable window's purchase confirmation to it, so
 * nobody types it; without the addon it is ".stable buyslot" in chat at a stable master.
 */

#include "CellImpl.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using namespace Acore::ChatCommands;

namespace
{
    std::string MoneyToString(uint32 copper)
    {
        uint32 const gold   = copper / 10000;
        uint32 const silver = (copper % 10000) / 100;
        uint32 const cop    = copper % 100;
        std::string out;
        if (gold)             out += Acore::StringFormat("{} gold", gold);
        if (silver)           out += Acore::StringFormat("{}{} silver", out.empty() ? "" : " ", silver);
        if (cop || out.empty()) out += Acore::StringFormat("{}{} copper", out.empty() ? "" : " ", cop);
        return out;
    }

    // A stable master the player can interact with right now (same test as the stable window).
    struct StableMasterInReach
    {
        Player* player;
        bool operator()(Creature* creature) const
        {
            return player->GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_STABLEMASTER) != nullptr;
        }
    };
}

class xorwow_stable_slot_commandscript : public CommandScript
{
public:
    xorwow_stable_slot_commandscript() : CommandScript("xorwow_stable_slot_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable stableCommandTable =
        {
            { "buyslot", HandleStableBuySlotCommand, SEC_PLAYER, Console::No }
        };
        static ChatCommandTable commandTable =
        {
            { "stable", stableCommandTable }
        };
        return commandTable;
    }

    static bool HandleStableBuySlotCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        Creature* stableMaster = nullptr;
        StableMasterInReach check{ player };
        Acore::CreatureLastSearcher<StableMasterInReach> searcher(player, stableMaster, check);
        Cell::VisitObjects(player, searcher, INTERACTION_DISTANCE * 2);
        if (!stableMaster)
        {
            handler->SendSysMessage("You must be standing at a stable master.");
            return true;
        }

        PetStable& petStable = player->GetOrInitPetStable();
        if (petStable.MaxStabledPets >= MAX_PET_STABLES)
        {
            handler->PSendSysMessage("You already have all {} stable slots.", uint32(MAX_PET_STABLES));
            return true;
        }

        uint8 const slot = petStable.MaxStabledPets + 1;
        uint32 const cost = sStableSlotPricesStore.LookupEntry(slot)->Price;
        if (!player->HasEnoughMoney(cost))
        {
            handler->PSendSysMessage("Stable slot {} costs {} - you have {}.", uint32(slot), MoneyToString(cost), MoneyToString(player->GetMoney()));
            return true;
        }

        WorldSession* session = handler->GetSession();
        WorldPacket packet(CMSG_BUY_STABLE_SLOT, 8);
        packet << stableMaster->GetGUID();
        session->HandleBuyStableSlot(packet);   // the stock purchase: checks, charges, adds the slot
        session->SendStablePet(stableMaster->GetGUID());

        if (petStable.MaxStabledPets >= slot)
            handler->PSendSysMessage("Stable slot {} bought for {}.", uint32(slot), MoneyToString(cost));
        else
            handler->SendSysMessage("The purchase did not go through - close and reopen the stable and try again.");
        return true;
    }
};

void AddSC_xorwow_stable_slot()
{
    new xorwow_stable_slot_commandscript();
}
