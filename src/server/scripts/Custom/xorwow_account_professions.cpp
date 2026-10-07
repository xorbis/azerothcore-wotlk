/*
 * XorWoW: account-bound gathering and secondary professions.
 *
 * Mining, Herbalism, Skinning, First Aid, Cooking and Fishing progress belongs to the account, not
 * to the character. A character still learns the profession at a trainer (so Mining, Herbalism and
 * Skinning still use one of its two primary profession slots), and the moment it does, it gets the
 * account's progress in it:
 *
 *   - the rank (Journeyman, Expert, Artisan...): the highest rank any character of the account has
 *     trained. Training a rank still needs the usual level and skill, so one character has to earn
 *     it at a trainer; every other character then gets it whatever its level;
 *   - the skill value: the highest any character of the account has reached (up to the rank's max);
 *   - the recipes: every recipe any character of the account has learned (trainer, recipe item,
 *     quest). Auto-learned skill spells (Find Minerals, Toughness...) come with the skill value.
 *
 * Progress made on any character counts for all of them: a skill-up or a new recipe is applied
 * right away to the account's other online characters (altbots) that have the profession, and to
 * the others when they next log in. Dropping a profession loses nothing: learn it again and it is
 * all back.
 *
 * Kept per account in xorwow_account_profession_skill / xorwow_account_profession_spell
 * (data/sql/custom/db_characters). An account starts with what its characters have when they log
 * in. Random bots (accounts with the playerbots random bot prefix) are left out; altbots on a
 * player's account take part like any other character of it.
 *
 * Threads: map updates run in parallel, so an account's state lives behind a mutex and a character
 * is only ever changed from its own Player::Update (OnPlayerUpdate notices the account moved on).
 */

#include "AccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

namespace
{
    constexpr uint32 SHARED_SKILLS[] = { SKILL_MINING, SKILL_HERBALISM, SKILL_SKINNING, SKILL_FIRST_AID, SKILL_COOKING, SKILL_FISHING };

    bool IsShared(uint32 skill)
    {
        return std::find(std::begin(SHARED_SKILLS), std::end(SHARED_SKILLS), skill) != std::end(SHARED_SKILLS);
    }

    // A profession rank spell (Apprentice Cooking, Journeyman Mining...) of a shared profession.
    SpellLearnSkillNode const* RankOf(uint32 spellId)
    {
        SpellLearnSkillNode const* node = sSpellMgr->GetSpellLearnSkill(spellId);
        return node && IsShared(node->skill) ? node : nullptr;
    }

    // The shared profession a recipe belongs to, 0 if it is not a recipe of one. Spells the skill
    // hands out by itself (Find Minerals, Toughness...) are not recipes: SetSkill deals with them.
    uint32 RecipeSkill(uint32 spellId)
    {
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (!IsShared(ability->SkillLine))
                continue;
            if (ability->AcquireMethod == SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE || ability->AcquireMethod == SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
                return 0;
            return ability->SkillLine;
        }
        return 0;
    }

    bool CanHaveRecipe(Player const* player, uint32 spellId)
    {
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (ability->RaceMask && !(ability->RaceMask & player->getRaceMask()))
                return false;
            if (ability->ClassMask && !(ability->ClassMask & player->getClassMask()))
                return false;
        }
        return true;
    }

    struct AccountProfessions
    {
        bool eligible = false;
        std::map<uint32, uint32> skills;   // skill -> best value
        std::set<uint32> spells;           // rank spells and recipes
        uint32 generation = 0;             // bumped whenever the account gains something
    };

    std::mutex accountsLock;
    std::unordered_map<uint32, AccountProfessions> accounts;
    std::atomic<uint32> anyGeneration{ 0 };   // bumped with any account's, so idle players skip the lock

    struct SeenGeneration : DataMap::Base
    {
        uint32 any = 0;
        uint32 account = 0;
    };
    char const* SEEN_KEY = "xorwow_account_professions";

    // Set while this thread is applying the account to a character, so the learnSpell/SetSkill
    // calls that makes do not start another sync.
    thread_local bool syncing = false;

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

    // Loads the account the first time one of its characters logs in. Returns whether it takes part.
    bool LoadAccount(uint32 accountId)
    {
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            auto itr = accounts.find(accountId);
            if (itr != accounts.end())
                return itr->second.eligible;
        }

        AccountProfessions loaded;
        loaded.eligible = !IsRandomBotAccount(accountId);
        if (loaded.eligible)
        {
            if (QueryResult result = CharacterDatabase.Query("SELECT `skill`, `value` FROM `xorwow_account_profession_skill` WHERE `account` = {}", accountId))
                do
                {
                    Field* fields = result->Fetch();
                    loaded.skills[fields[0].Get<uint32>()] = fields[1].Get<uint32>();
                } while (result->NextRow());

            if (QueryResult result = CharacterDatabase.Query("SELECT `spell` FROM `xorwow_account_profession_spell` WHERE `account` = {}", accountId))
                do
                {
                    loaded.spells.insert(result->Fetch()[0].Get<uint32>());
                } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(accountsLock);
        return accounts.emplace(accountId, std::move(loaded)).first->second.eligible;
    }

    // Callers hold accountsLock.
    AccountProfessions* FindEligible(uint32 accountId)
    {
        auto itr = accounts.find(accountId);
        return itr != accounts.end() && itr->second.eligible ? &itr->second : nullptr;
    }

    bool RecordSkillLocked(AccountProfessions& account, uint32 accountId, uint32 skill, uint32 value)
    {
        uint32& best = account.skills[skill];
        if (value <= best)
            return false;
        best = value;
        CharacterDatabase.Execute("INSERT INTO `xorwow_account_profession_skill` (`account`, `skill`, `value`) VALUES ({}, {}, {}) "
            "ON DUPLICATE KEY UPDATE `value` = GREATEST(`value`, VALUES(`value`))", accountId, skill, value);
        return true;
    }

    bool RecordSpellLocked(AccountProfessions& account, uint32 accountId, uint32 spellId)
    {
        if (!account.spells.insert(spellId).second)
            return false;
        CharacterDatabase.Execute("INSERT IGNORE INTO `xorwow_account_profession_spell` (`account`, `spell`) VALUES ({}, {})", accountId, spellId);
        return true;
    }

    void Bump(AccountProfessions& account)
    {
        ++account.generation;
        ++anyGeneration;
    }

    // Only characters of a taking-part account get this at login, so random bots stop here.
    bool TakesPart(Player* player)
    {
        return player->CustomData.Get<SeenGeneration>(SEEN_KEY) != nullptr;
    }

    void RecordSkill(Player* player, uint32 skill, uint32 value)
    {
        if (!IsShared(skill) || !value || !TakesPart(player))
            return;
        uint32 const accountId = player->GetSession()->GetAccountId();
        std::lock_guard<std::mutex> guard(accountsLock);
        if (AccountProfessions* account = FindEligible(accountId))
            if (RecordSkillLocked(*account, accountId, skill, value))
                Bump(*account);
    }

    void RecordSpell(Player* player, uint32 spellId)
    {
        uint32 const accountId = player->GetSession()->GetAccountId();
        std::lock_guard<std::mutex> guard(accountsLock);
        if (AccountProfessions* account = FindEligible(accountId))
            if (RecordSpellLocked(*account, accountId, spellId))
                Bump(*account);
    }

    // Everything the character brings to the account: run at login.
    void RecordAll(Player* player)
    {
        uint32 const accountId = player->GetSession()->GetAccountId();
        std::lock_guard<std::mutex> guard(accountsLock);
        AccountProfessions* account = FindEligible(accountId);
        if (!account)
            return;

        bool gained = false;
        for (uint32 skill : SHARED_SKILLS)
            if (player->HasSkill(skill))
                gained |= RecordSkillLocked(*account, accountId, skill, player->GetPureSkillValue(skill));

        for (auto const& [spellId, spell] : player->GetSpellMap())
            if (spell->State != PLAYERSPELL_REMOVED && (RankOf(spellId) || RecipeSkill(spellId)))
                gained |= RecordSpellLocked(*account, accountId, spellId);

        if (gained)
            Bump(*account);
    }

    // Gives the character the account's progress in the shared professions it has.
    void Sync(Player* player)
    {
        if (syncing)
            return;

        uint32 const accountId = player->GetSession()->GetAccountId();
        AccountProfessions account;
        {
            std::lock_guard<std::mutex> guard(accountsLock);
            AccountProfessions* found = FindEligible(accountId);
            if (!found)
                return;
            account = *found;
        }

        SeenGeneration* seen = player->CustomData.GetDefault<SeenGeneration>(SEEN_KEY);
        seen->account = account.generation;

        syncing = true;
        std::string gains;
        for (uint32 skill : SHARED_SKILLS)
        {
            if (!player->HasSkill(skill))
                continue;

            // The highest rank anyone on the account trained, with the ranks below it.
            SpellLearnSkillNode const* bestRank = nullptr;
            uint32 bestRankSpell = 0;
            for (uint32 spellId : account.spells)
                if (SpellLearnSkillNode const* rank = RankOf(spellId))
                    if (rank->skill == skill && (!bestRank || rank->step > bestRank->step))
                    {
                        bestRank = rank;
                        bestRankSpell = spellId;
                    }

            uint16 const stepBefore = player->GetSkillStep(skill);
            if (bestRank && bestRank->step > stepBefore)
                for (uint32 spellId = sSpellMgr->GetFirstSpellInChain(bestRankSpell); spellId; spellId = sSpellMgr->GetNextSpellInChain(spellId))
                {
                    if (!player->HasSpell(spellId))
                        player->learnSpell(spellId);
                    if (spellId == bestRankSpell)
                        break;
                }
            if (bestRank && !player->HasSpell(bestRankSpell))
                player->learnSpell(bestRankSpell);   // not chained in spell_ranks

            uint16 const valueBefore = player->GetPureSkillValue(skill);
            auto best = account.skills.find(skill);
            if (best != account.skills.end())
            {
                uint16 const max = player->GetPureMaxSkillValue(skill);
                uint16 const value = std::min<uint32>(best->second, max);
                if (value > valueBefore)
                    player->SetSkill(skill, player->GetSkillStep(skill), value, max);
            }

            uint32 recipes = 0;
            for (uint32 spellId : account.spells)
                if (RecipeSkill(spellId) == skill && !player->HasSpell(spellId) && CanHaveRecipe(player, spellId))
                {
                    player->learnSpell(spellId);
                    if (player->HasSpell(spellId))
                        ++recipes;
                }

            uint16 const valueAfter = player->GetPureSkillValue(skill);
            if (valueAfter == valueBefore && player->GetSkillStep(skill) == stepBefore && !recipes)
                continue;

            SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
            std::string text = Acore::StringFormat("{} {}/{}", line ? line->name[0] : "?", valueAfter, player->GetPureMaxSkillValue(skill));
            if (recipes)
                text += Acore::StringFormat(" (+{} recipe{})", recipes, recipes == 1 ? "" : "s");
            gains += gains.empty() ? text : ", " + text;
        }
        syncing = false;

        if (!gains.empty() && player->IsInWorld())
            ChatHandler(player->GetSession()).SendSysMessage(Acore::StringFormat("|cff66ccffAccount professions:|r {}", gains));
    }
}

class xorwow_account_professions_playerscript : public PlayerScript
{
public:
    xorwow_account_professions_playerscript() : PlayerScript("xorwow_account_professions_playerscript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_ON_UPDATE_SKILL, PLAYERHOOK_ON_SET_SKILL, PLAYERHOOK_ON_UPDATE }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!LoadAccount(player->GetSession()->GetAccountId()))
            return;
        player->CustomData.GetDefault<SeenGeneration>(SEEN_KEY)->any = anyGeneration;
        RecordAll(player);
        Sync(player);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (!TakesPart(player))
            return;
        bool const rank = RankOf(spellId) != nullptr;
        if (!rank && !RecipeSkill(spellId))
            return;
        RecordSpell(player, spellId);
        // Learning a shared profession (or a rank of it) at a trainer: the rest comes from the account.
        if (rank && !syncing && player->IsInWorld())
            Sync(player);
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/, uint32 /*step*/, uint32 newValue) override
    {
        RecordSkill(player, skillId, newValue);
    }

    void OnPlayerSetSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/, uint32 /*step*/, uint32 newValue) override
    {
        RecordSkill(player, skillId, newValue);   // 0 = profession dropped: the account keeps it
    }

    // Progress made by another character of the account reaches this one here, on its own map thread.
    void OnPlayerUpdate(Player* player, uint32 /*p_time*/) override
    {
        uint32 const any = anyGeneration;
        SeenGeneration* seen = player->CustomData.Get<SeenGeneration>(SEEN_KEY);
        if (!seen || seen->any == any || !player->IsInWorld())
            return;
        seen->any = any;

        {
            std::lock_guard<std::mutex> guard(accountsLock);
            AccountProfessions* account = FindEligible(player->GetSession()->GetAccountId());
            if (!account || account->generation == seen->account)
                return;
        }
        Sync(player);
    }
};

void AddSC_xorwow_account_professions()
{
    new xorwow_account_professions_playerscript();
}
