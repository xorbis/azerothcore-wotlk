/*
 * XorWoW: flight paths are shared by the account.
 *
 * A flight path any character of the account has discovered is known by all of them, as far as
 * its faction can fly it: a Horde character gets the account's Horde and neutral flight masters
 * (Booty Bay, Gadgetzan, Everlook...), never the Alliance ones, and the other way round. A
 * character logging in gets the account's flight paths; one discovered on another character
 * reaches the account's online characters (altbots) within a second.
 *
 * Death knights start out knowing every old-continent flight master of both factions, so their
 * Eastern Kingdoms and Kalimdor flight paths are not taken as discoveries: they get the account's
 * flight paths but only add the others (Outland, Northrend). The death knight-only flight masters
 * (Acherus, the Shadow Vault) are never given to other classes.
 *
 * Kept per account in xorwow_account_taxi (data/sql/custom/db_characters). The first time an
 * account is loaded it starts with what its characters know. Random bots (accounts with the
 * playerbots random bot prefix) are left out; altbots on a player's account take part like any
 * other character of it.
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
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace
{
    constexpr uint32 SCAN_INTERVAL = 1000;   // ms between looks at a character's own discoveries

    struct AccountTaxi
    {
        bool eligible = false;
        TaxiMask mask{};
        uint32 generation = 0;   // bumped whenever the account discovers a flight path
    };

    std::mutex accountsLock;
    std::unordered_map<uint32, AccountTaxi> accounts;
    std::atomic<uint32> anyGeneration{ 0 };   // bumped with any account's, so idle players skip the lock

    struct PlayerTaxiState : DataMap::Base
    {
        uint32 any = 0;
        uint32 account = 0;
        uint32 scanTimer = SCAN_INTERVAL;
        TaxiMask known{};   // the character's flight paths as of its last scan or sync
    };
    char const* STATE_KEY = "xorwow_account_taxi";

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

    std::string ToText(TaxiMask const& mask)
    {
        std::string text;
        for (uint32 value : mask)
            text += std::to_string(value) + ' ';
        return text;
    }

    void MergeText(TaxiMask& mask, std::string_view text)
    {
        std::vector<std::string_view> tokens = Acore::Tokenize(text, ' ', false);
        for (size_t i = 0; i < tokens.size() && i < mask.size(); ++i)
            if (Optional<uint32> value = Acore::StringTo<uint32>(tokens[i]))
                mask[i] |= *value & sTaxiNodesMask[i];
    }

    // The flight paths a character of this race and class may have from the account.
    TaxiMask Usable(uint8 race, uint8 chrClass)
    {
        TaxiMask const& faction = Player::TeamIdForRace(race) == TEAM_ALLIANCE ? sAllianceTaxiNodesMask : sHordeTaxiNodesMask;
        TaxiMask usable;
        for (size_t i = 0; i < TaxiMaskSize; ++i)
            usable[i] = chrClass == CLASS_DEATH_KNIGHT ? faction[i] | sDeathKnightTaxiNodesMask[i] : faction[i] & ~sDeathKnightTaxiNodesMask[i];
        return usable;
    }

    // The flight paths of a character that count as its own discoveries.
    TaxiMask Discovered(TaxiMask const& known, uint8 race, uint8 chrClass)
    {
        TaxiMask const usable = Usable(race, chrClass);
        TaxiMask discovered;
        for (size_t i = 0; i < TaxiMaskSize; ++i)
        {
            discovered[i] = known[i] & usable[i];
            if (chrClass == CLASS_DEATH_KNIGHT)
                discovered[i] &= ~sOldContinentsNodesMask[i];   // all given at creation
        }
        return discovered;
    }

    TaxiMask ReadPlayer(Player* player)
    {
        TaxiMask mask{};
        for (uint32 node = 1; node <= TaxiMaskSize * 32; ++node)
            if (player->m_taxi.IsTaximaskNodeKnown(node))
                mask[(node - 1) / 32] |= 1u << ((node - 1) % 32);
        return mask;
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

        AccountTaxi loaded;
        loaded.eligible = !IsRandomBotAccount(accountId);
        if (loaded.eligible)
        {
            if (QueryResult result = CharacterDatabase.Query("SELECT `taximask` FROM `xorwow_account_taxi` WHERE `account` = {}", accountId))
                MergeText(loaded.mask, result->Fetch()[0].Get<std::string_view>());
            if (QueryResult result = CharacterDatabase.Query("SELECT `race`, `class`, `taximask` FROM `characters` WHERE `account` = {}", accountId))
                do
                {
                    Field* fields = result->Fetch();
                    TaxiMask known{};
                    MergeText(known, fields[2].Get<std::string_view>());
                    TaxiMask const discovered = Discovered(known, fields[0].Get<uint8>(), fields[1].Get<uint8>());
                    for (size_t i = 0; i < TaxiMaskSize; ++i)
                        loaded.mask[i] |= discovered[i];
                } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(accountsLock);
        auto [itr, inserted] = accounts.emplace(accountId, std::move(loaded));
        if (inserted && itr->second.eligible)
            CharacterDatabase.Execute("REPLACE INTO `xorwow_account_taxi` (`account`, `taximask`) VALUES ({}, '{}')", accountId, ToText(itr->second.mask));
        return itr->second.eligible;
    }

    // Adds the character's discoveries to the account.
    void Record(Player* player, PlayerTaxiState& state, TaxiMask const& current)
    {
        state.known = current;
        TaxiMask const discovered = Discovered(current, player->getRace(), player->getClass());

        uint32 const accountId = player->GetSession()->GetAccountId();
        std::lock_guard<std::mutex> guard(accountsLock);
        auto itr = accounts.find(accountId);
        if (itr == accounts.end() || !itr->second.eligible)
            return;

        AccountTaxi& account = itr->second;
        bool gained = false;
        for (size_t i = 0; i < TaxiMaskSize; ++i)
            if (discovered[i] & ~account.mask[i])
            {
                account.mask[i] |= discovered[i];
                gained = true;
            }
        if (!gained)
            return;

        ++account.generation;
        ++anyGeneration;
        CharacterDatabase.Execute("REPLACE INTO `xorwow_account_taxi` (`account`, `taximask`) VALUES ({}, '{}')", accountId, ToText(account.mask));
    }

    // Gives the character the account's flight paths its faction can fly.
    void Sync(Player* player, PlayerTaxiState& state)
    {
        TaxiMask mask;
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(player->GetSession()->GetAccountId());
            if (itr == accounts.end() || !itr->second.eligible)
                return;
            mask = itr->second.mask;
            state.account = itr->second.generation;
        }

        TaxiMask const usable = Usable(player->getRace(), player->getClass());
        uint32 learned = 0;
        for (uint32 node = 1; node <= TaxiMaskSize * 32; ++node)
        {
            uint32 const field = (node - 1) / 32;
            uint32 const submask = 1u << ((node - 1) % 32);
            if ((mask[field] & usable[field] & submask) && player->m_taxi.SetTaximaskNode(node))
                ++learned;
        }
        state.known = ReadPlayer(player);

        if (learned && player->IsInWorld())
            ChatHandler(player->GetSession()).SendSysMessage(Acore::StringFormat("|cff66ccffAccount flight paths:|r {} new flight path{}", learned, learned == 1 ? "" : "s"));
    }
}

class xorwow_account_taxi_playerscript : public PlayerScript
{
public:
    xorwow_account_taxi_playerscript() : PlayerScript("xorwow_account_taxi_playerscript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!LoadAccount(player->GetSession()->GetAccountId()))
            return;
        PlayerTaxiState* state = player->CustomData.GetDefault<PlayerTaxiState>(STATE_KEY);
        state->any = anyGeneration;
        Record(player, *state, ReadPlayer(player));
        Sync(player, *state);
    }

    void OnPlayerUpdate(Player* player, uint32 p_time) override
    {
        PlayerTaxiState* state = player->CustomData.Get<PlayerTaxiState>(STATE_KEY);
        if (!state || !player->IsInWorld())
            return;

        // This character's own discoveries go to the account.
        if (state->scanTimer <= p_time)
        {
            state->scanTimer = SCAN_INTERVAL;
            TaxiMask const current = ReadPlayer(player);
            if (current != state->known)
                Record(player, *state, current);
        }
        else
            state->scanTimer -= p_time;

        // Another character's discoveries reach this one.
        uint32 const any = anyGeneration;
        if (state->any == any)
            return;
        state->any = any;
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(player->GetSession()->GetAccountId());
            if (itr == accounts.end() || itr->second.generation == state->account)
                return;
        }
        Sync(player, *state);
    }
};

void AddSC_xorwow_account_taxi()
{
    new xorwow_account_taxi_playerscript();
}
