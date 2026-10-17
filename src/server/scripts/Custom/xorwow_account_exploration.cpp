/*
 * XorWoW: explored zones are shared by the account.
 *
 * The world map's explored areas (the PLAYER_EXPLORED_ZONES bits, characters.exploredZones) belong
 * to the account, not to the character: an area any character of the account has discovered is
 * discovered for all of them. A character logging in gets the account's map; an area discovered on
 * one character reaches the account's other online characters (altbots) within a second.
 *
 * What follows from sharing the bits:
 *   - the exploration achievements (Explore Elwynn Forest...) complete on every character once the
 *     account has the areas;
 *   - an area the account already knows gives no "Discovered" message or exploration XP to the
 *     other characters: for them it is already explored.
 *
 * Kept per account in xorwow_account_explored (data/sql/custom/db_characters). The first time an
 * account is loaded it starts with the union of all its characters' maps. Random bots (accounts
 * with the playerbots random bot prefix) are left out; altbots on a player's account take part
 * like any other character of it.
 *
 * Threads: map updates run in parallel, so an account's state lives behind a mutex and a character
 * is only ever changed from its own Player::Update.
 */

#include "AccountMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include <array>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace
{
    using ExploredMask = std::array<uint32, PLAYER_EXPLORED_ZONES_SIZE>;

    constexpr uint32 SCAN_INTERVAL = 1000;   // ms between looks at a character's own discoveries

    struct AccountExploration
    {
        bool eligible = false;
        ExploredMask mask{};
        uint32 generation = 0;   // bumped whenever the account discovers something
    };

    std::mutex accountsLock;
    std::unordered_map<uint32, AccountExploration> accounts;
    std::atomic<uint32> anyGeneration{ 0 };   // bumped with any account's, so idle players skip the lock

    struct PlayerExploration : DataMap::Base
    {
        uint32 any = 0;
        uint32 account = 0;
        uint32 scanTimer = SCAN_INTERVAL;
        ExploredMask known{};   // the character's map as of its last scan or sync
    };
    char const* STATE_KEY = "xorwow_account_exploration";

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

    void MergeText(ExploredMask& mask, std::string_view text)
    {
        std::vector<std::string_view> tokens = Acore::Tokenize(text, ' ', false);
        for (size_t i = 0; i < tokens.size() && i < mask.size(); ++i)
            if (Optional<uint32> value = Acore::StringTo<uint32>(tokens[i]))
                mask[i] |= *value;
    }

    std::string ToText(ExploredMask const& mask)
    {
        std::string text;
        for (uint32 value : mask)
            text += std::to_string(value) + ' ';
        return text;
    }

    ExploredMask ReadPlayer(Player const* player)
    {
        ExploredMask mask;
        for (uint32 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
            mask[i] = player->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + i);
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

        AccountExploration loaded;
        loaded.eligible = !IsRandomBotAccount(accountId);
        if (loaded.eligible)
        {
            if (QueryResult result = CharacterDatabase.Query("SELECT `explored` FROM `xorwow_account_explored` WHERE `account` = {}", accountId))
                MergeText(loaded.mask, result->Fetch()[0].Get<std::string_view>());
            if (QueryResult result = CharacterDatabase.Query("SELECT `exploredZones` FROM `characters` WHERE `account` = {}", accountId))
                do
                {
                    MergeText(loaded.mask, result->Fetch()[0].Get<std::string_view>());
                } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(accountsLock);
        auto [itr, inserted] = accounts.emplace(accountId, std::move(loaded));
        if (inserted && itr->second.eligible)
            CharacterDatabase.Execute("REPLACE INTO `xorwow_account_explored` (`account`, `explored`) VALUES ({}, '{}')", accountId, ToText(itr->second.mask));
        return itr->second.eligible;
    }

    // Adds the character's discoveries to the account.
    void Record(Player* player, PlayerExploration& state, ExploredMask const& current)
    {
        uint32 const accountId = player->GetSession()->GetAccountId();
        std::lock_guard<std::mutex> guard(accountsLock);
        auto itr = accounts.find(accountId);
        if (itr == accounts.end() || !itr->second.eligible)
            return;

        AccountExploration& account = itr->second;
        bool gained = false;
        for (uint32 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
            if (current[i] & ~account.mask[i])
            {
                account.mask[i] |= current[i];
                gained = true;
            }
        state.known = current;
        if (!gained)
            return;

        ++account.generation;
        ++anyGeneration;
        CharacterDatabase.Execute("REPLACE INTO `xorwow_account_explored` (`account`, `explored`) VALUES ({}, '{}')", accountId, ToText(account.mask));
    }

    // Gives the character the account's map.
    void Sync(Player* player, PlayerExploration& state)
    {
        ExploredMask mask;
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(player->GetSession()->GetAccountId());
            if (itr == accounts.end() || !itr->second.eligible)
                return;
            mask = itr->second.mask;
            state.account = itr->second.generation;
        }

        bool gained = false;
        for (uint32 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
        {
            uint32 const value = player->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + i);
            if (mask[i] & ~value)
            {
                player->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + i, value | mask[i]);
                gained = true;
            }
            state.known[i] = value | mask[i];
        }

        // The exploration criteria look at the whole map, so one update completes whatever the new areas finish.
        if (gained)
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA);
    }
}

class xorwow_account_exploration_playerscript : public PlayerScript
{
public:
    xorwow_account_exploration_playerscript() : PlayerScript("xorwow_account_exploration_playerscript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!LoadAccount(player->GetSession()->GetAccountId()))
            return;
        PlayerExploration* state = player->CustomData.GetDefault<PlayerExploration>(STATE_KEY);
        state->any = anyGeneration;
        Record(player, *state, ReadPlayer(player));
        Sync(player, *state);
    }

    void OnPlayerUpdate(Player* player, uint32 p_time) override
    {
        PlayerExploration* state = player->CustomData.Get<PlayerExploration>(STATE_KEY);
        if (!state || !player->IsInWorld())
            return;

        // This character's own discoveries go to the account.
        if (state->scanTimer <= p_time)
        {
            state->scanTimer = SCAN_INTERVAL;
            ExploredMask const current = ReadPlayer(player);
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

void AddSC_xorwow_account_exploration()
{
    new xorwow_account_exploration_playerscript();
}
