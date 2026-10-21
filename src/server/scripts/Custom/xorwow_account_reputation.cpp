/*
 * XorWoW: reputation is shared by the account.
 *
 * What a character earns with a faction, gains and losses alike, is earned by every character of
 * the account. Only the earned part is shared: each character keeps its own race and class
 * starting standing (a night elf's head start with Darnassus, a druid's with the Cenarion Circle)
 * and gets the account's earnings on top of it, up to Exalted.
 *
 * A faction closed to one side (it starts at Hated for that side's races: the capital cities,
 * Silverwing Sentinels and Warsong Outriders, Stormpike Guard and Frostwolf Clan, Honor Hold and
 * Thrallmar...) is shared only by the characters of the same side. The others (Argent Dawn,
 * Timbermaw Hold, the Cenarion Circle, the Steamwheedle Cartel...) are shared by the whole account.
 * The hidden crafting specialisation factions stay per character, like crafting itself.
 *
 * A character logging in gets the account's reputation; one earned on another character reaches
 * the account's online characters (altbots) within a second.
 *
 * Kept per account in xorwow_account_reputation (data/sql/custom/db_characters). The first time an
 * account is loaded it starts with the best standing its characters have earned with each faction.
 * Random bots (accounts with the playerbots random bot prefix) are left out; altbots on a player's
 * account take part like any other character of it.
 *
 * Threads: map updates run in parallel, so an account's state lives behind a mutex and a character
 * is only ever changed from its own Player::Update.
 */

#include "AccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr uint32 SCAN_INTERVAL = 1000;   // ms between looks at a character's own reputation

    enum Side : uint8
    {
        SIDE_ACCOUNT  = 0,   // shared by every character of the account
        SIDE_ALLIANCE = 1,
        SIDE_HORDE    = 2,
    };

    // Never shared: crafting specialisations, test, placeholder and conversion entries.
    std::unordered_set<uint32> const EXCLUDED =
    {
        46, 289, 569, 570, 571,   // Blacksmithing - Armorsmithing, Weaponsmithing, Hammersmithing, Axesmithing, Swordsmithing
        83, 86, 549,              // Leatherworking - Elemental, Dragonscale, Tribal
        550, 551,                 // Engineering - Goblin, Gnome
        948, 949, 952,            // Test Faction 1-3
        1005, 1082,               // Friendly, Hidden; REUSE
        1136, 1137, 1154, 1155,   // Tranquillien, Wintersaber, Silver Covenant, Sunreavers Conversion
    };

    // faction id -> earned standing (FactionState::Standing, the part above the base)
    using Earned = std::map<uint32, int32>;

    struct AccountReputation
    {
        bool eligible = false;
        Earned sides[3];
        uint32 generation = 0;   // bumped whenever the account's reputation changes
    };

    std::mutex accountsLock;
    std::unordered_map<uint32, AccountReputation> accounts;
    std::atomic<uint32> anyGeneration{ 0 };   // bumped with any account's, so idle players skip the lock

    struct PlayerReputationState : DataMap::Base
    {
        uint32 any = 0;
        uint32 account = 0;
        uint32 scanTimer = SCAN_INTERVAL;
        Earned known;   // the character's earned standings as of its last exchange with the account
    };
    char const* STATE_KEY = "xorwow_account_reputation";

    bool IsRandomBotAccount(uint32 accountId)
    {
        std::string name;
        if (!AccountMgr::GetName(accountId, name))
            return true;
        std::string prefix = sConfigMgr->GetOption<std::string>("AiPlayerbot.RandomBotAccountPrefix", "rndbot", false);
        if (prefix.empty() || name.size() < prefix.size())
            return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (std::toupper(static_cast<unsigned char>(name[i])) != std::toupper(static_cast<unsigned char>(prefix[i])))
                return false;
        return true;
    }

    // Faction.dbc's base standing for a race, as ReputationMgr::GetBaseReputation reads it.
    int32 RaceBase(FactionEntry const* faction, uint8 race)
    {
        uint32 const raceMask = 1u << (race - 1);
        for (int i = 0; i < 4; ++i)
            if ((faction->BaseRepRaceMask[i] & raceMask) || (faction->BaseRepRaceMask[i] == 0 && faction->BaseRepClassMask[i] != 0))
                return faction->BaseRepValue[i];
        return 0;
    }

    // Whether some playable races start at Hated with the faction and others do not.
    bool ClosedToASide(FactionEntry const* faction)
    {
        bool hated = false, open = false;
        for (uint8 race : { RACE_HUMAN, RACE_ORC, RACE_DWARF, RACE_NIGHTELF, RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_GNOME, RACE_TROLL,
            RACE_BLOODELF, RACE_DRAENEI })
            (RaceBase(faction, race) <= ReputationMgr::Reputation_Bottom ? hated : open) = true;
        return hated && open;
    }

    // Which of the account's pools a faction is shared in for a character of this race, if any.
    Optional<Side> SideOf(uint32 factionId, uint8 race)
    {
        if (EXCLUDED.count(factionId))
            return {};
        FactionEntry const* faction = sFactionStore.LookupEntry(factionId);
        if (!faction || !faction->CanHaveReputation())
            return {};
        if (!ClosedToASide(faction))
            return SIDE_ACCOUNT;
        return Player::TeamIdForRace(race) == TEAM_ALLIANCE ? SIDE_ALLIANCE : SIDE_HORDE;
    }

    Earned ReadPlayer(Player* player)
    {
        Earned earned;
        for (auto const& [listId, state] : player->GetReputationMgr().GetStateList())
            if (SideOf(state.ID, player->getRace()))
                earned[state.ID] = state.Standing;
        return earned;
    }

    void Save(uint32 accountId, uint8 side, uint32 factionId, int32 standing)
    {
        CharacterDatabase.Execute("REPLACE INTO `xorwow_account_reputation` (`account`, `side`, `faction`, `standing`) VALUES ({}, {}, {}, {})",
            accountId, side, factionId, standing);
    }

    // Loads the account the first time one of its characters logs in. Returns whether it takes part.
    bool LoadAccount(uint32 accountId)
    {
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(accountId);
            if (itr != accounts.end())
                return itr->second.eligible;
        }

        AccountReputation loaded;
        loaded.eligible = !IsRandomBotAccount(accountId);
        bool seeded = false;
        if (loaded.eligible)
        {
            if (QueryResult result = CharacterDatabase.Query("SELECT `side`, `faction`, `standing` FROM `xorwow_account_reputation` WHERE `account` = {}", accountId))
            {
                seeded = true;
                do
                {
                    Field* fields = result->Fetch();
                    uint8 const side = fields[0].Get<uint8>();
                    if (side <= SIDE_HORDE)
                        loaded.sides[side][fields[1].Get<uint32>()] = fields[2].Get<int32>();
                } while (result->NextRow());
            }
            // First load: the best standing any of the account's characters has earned.
            if (!seeded)
                if (QueryResult result = CharacterDatabase.Query("SELECT c.`race`, r.`faction`, r.`standing` FROM `character_reputation` r "
                    "JOIN `characters` c ON c.`guid` = r.`guid` WHERE c.`account` = {} AND r.`standing` <> 0", accountId))
                    do
                    {
                        Field* fields = result->Fetch();
                        uint32 const factionId = fields[1].Get<uint32>();
                        int32 const standing = fields[2].Get<int32>();
                        if (Optional<Side> side = SideOf(factionId, fields[0].Get<uint8>()))
                        {
                            auto [itr, inserted] = loaded.sides[*side].emplace(factionId, standing);
                            if (!inserted && standing > itr->second)
                                itr->second = standing;
                        }
                    } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(accountsLock);
        auto [itr, inserted] = accounts.emplace(accountId, std::move(loaded));
        if (inserted && itr->second.eligible && !seeded)
            for (uint8 side = SIDE_ACCOUNT; side <= SIDE_HORDE; ++side)
                for (auto const& [factionId, standing] : itr->second.sides[side])
                    Save(accountId, side, factionId, standing);
        return itr->second.eligible;
    }

    // Adds what the character earned or lost since the last exchange to the account, then gives the
    // character the account's reputation. Done in one go so neither side's changes are lost.
    void Exchange(Player* player, PlayerReputationState& state)
    {
        uint32 const accountId = player->GetSession()->GetAccountId();
        uint8 const race = player->getRace();
        Earned const current = ReadPlayer(player);
        Earned target;
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(accountId);
            if (itr == accounts.end() || !itr->second.eligible)
                return;

            AccountReputation& account = itr->second;
            bool changed = false;
            for (auto const& [factionId, standing] : current)
            {
                auto known = state.known.find(factionId);
                int32 const delta = standing - (known != state.known.end() ? known->second : 0);
                if (!delta)
                    continue;
                Side const side = *SideOf(factionId, race);
                int32& shared = account.sides[side][factionId];
                shared = std::clamp(shared + delta, ReputationMgr::Reputation_Bottom * 2, ReputationMgr::Reputation_Cap * 2);
                Save(accountId, side, factionId, shared);
                changed = true;
            }
            if (changed)
            {
                ++account.generation;
                ++anyGeneration;
            }
            state.account = account.generation;

            uint8 const own = Player::TeamIdForRace(race) == TEAM_ALLIANCE ? SIDE_ALLIANCE : SIDE_HORDE;
            for (uint8 side : { uint8(SIDE_ACCOUNT), own })
                for (auto const& [factionId, standing] : account.sides[side])
                    if (SideOf(factionId, race) == Side(side))
                        target[factionId] = standing;
        }

        ReputationMgr& reputation = player->GetReputationMgr();
        FactionState const* changed = nullptr;
        uint32 raised = 0;
        for (auto const& [factionId, standing] : target)
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(factionId);
            FactionState const* own = faction ? reputation.GetState(faction) : nullptr;
            if (!own || own->Standing == standing)
                continue;
            int32 const base = reputation.GetBaseReputation(faction);
            int32 const total = std::clamp(base + standing, ReputationMgr::Reputation_Bottom, ReputationMgr::Reputation_Cap);
            if (total == base + own->Standing)
                continue;   // already at the cap or the bottom
            if (total > base + own->Standing)
                ++raised;
            if (reputation.SetOneFactionReputation(faction, float(total), false))
                changed = own;
        }
        if (changed)
            reputation.SendState(changed);   // sends every faction marked as changed in one packet
        state.known = ReadPlayer(player);

        if (raised && player->IsInWorld())
            ChatHandler(player->GetSession()).SendSysMessage(Acore::StringFormat("|cff66ccffAccount reputation:|r {} faction{} raised", raised, raised == 1 ? "" : "s"));
    }
}

class xorwow_account_reputation_playerscript : public PlayerScript
{
public:
    xorwow_account_reputation_playerscript() : PlayerScript("xorwow_account_reputation_playerscript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!LoadAccount(player->GetSession()->GetAccountId()))
            return;
        PlayerReputationState* state = player->CustomData.GetDefault<PlayerReputationState>(STATE_KEY);
        state->any = anyGeneration;
        // Everything this character has is new to the account only on the account's first load;
        // otherwise the account already counts it, so start from the character's own standings.
        state->known = ReadPlayer(player);
        Exchange(player, *state);
    }

    void OnPlayerUpdate(Player* player, uint32 p_time) override
    {
        PlayerReputationState* state = player->CustomData.Get<PlayerReputationState>(STATE_KEY);
        if (!state || !player->IsInWorld())
            return;

        if (state->scanTimer > p_time)
        {
            state->scanTimer -= p_time;
            return;
        }
        state->scanTimer = SCAN_INTERVAL;

        // This character's own gains and losses, or another character's.
        uint32 const any = anyGeneration;
        bool otherChanged = false;
        if (state->any != any)
        {
            state->any = any;
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(player->GetSession()->GetAccountId());
            otherChanged = itr != accounts.end() && itr->second.generation != state->account;
        }
        if (otherChanged || ReadPlayer(player) != state->known)
            Exchange(player, *state);
    }
};

void AddSC_xorwow_account_reputation()
{
    new xorwow_account_reputation_playerscript();
}
