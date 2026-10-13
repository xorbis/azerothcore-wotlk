/*
 * XorWoW TCG: the guild hall Card Vendor's goods, the only place cards are sold.
 *
 * Every server day (local midnight, the day the TCG's flee count uses) the vendor sells 15 cards
 * picked from the open ones (era <= the level cap's, xorwow_tcg.cpp): 5 common, 4 uncommon, 3 rare,
 * 2 epic and 1 legendary, by the card item's quality (a card's quality follows its tier). The pick
 * is seeded by the day, so a restart keeps the day's list, and both factions' vendors (the gnome
 * and the goblin, client-patch/guild_hall_catalog.py items 430 and 431) sell the same cards.
 * Prices are the card items' BuyPrice (scripts\client-patch\build_tcg.py: 75, 150, 250, 500 and
 * 1000 gold); they sell for nothing and the auction house bot neither lists nor buys them
 * (AuctionHouseBot.DisabledCustomItemIDs). No stock limit.
 *
 * The goods live in the core's vendor cache only (npc_vendor has none for these entries), set in
 * the world update after the map threads, as game events change vendors; a ".reload npc_vendor"
 * empties them and the next check puts them back.
 */

#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "Timer.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <random>
#include <vector>

namespace
{
    constexpr uint32 ITEM_BASE = 261000;                                   // card N is item 261000 + N
    constexpr std::array<uint32, 2> VENDORS = { 260430, 260431 };          // guild hall catalogue 430, 431 (260000 + id)
    constexpr std::array<uint32, 6> DAILY_BY_QUALITY = { 0, 5, 4, 3, 2, 1 };   // white, green, blue, purple, orange
    constexpr uint32 CHECK_MS = 60 * IN_MILLISECONDS;

    struct CardItem
    {
        uint32 item;
        uint32 era;
    };

    std::vector<CardItem> cardItems;
    std::vector<uint32> goods;          // the day's items, in slot order
    time_t goodsDay = 0;

    uint32 OpenEra()
    {
        uint32 cap = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        return cap >= 80 ? 2 : cap >= 70 ? 1 : 0;
    }

    void LoadCardItems()
    {
        cardItems.clear();
        if (QueryResult result = WorldDatabase.Query("SELECT `item`, `era` FROM `xorwow_tcg_card` ORDER BY `id`"))
        {
            do
            {
                Field* f = result->Fetch();
                cardItems.push_back({ f[0].Get<uint32>(), f[1].Get<uint32>() });
            } while (result->NextRow());
        }
    }

    std::vector<uint32> PickGoods(time_t day)
    {
        std::array<std::vector<uint32>, DAILY_BY_QUALITY.size()> byQuality;
        uint32 era = OpenEra();
        for (CardItem const& card : cardItems)
            if (card.era <= era && card.item > ITEM_BASE)
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(card.item))
                    if (proto->Quality < byQuality.size())
                        byQuality[proto->Quality].push_back(card.item);

        std::mt19937 rng(uint32(day / DAY));
        std::vector<uint32> picked;
        for (uint32 quality = 0; quality < byQuality.size(); ++quality)
        {
            std::vector<uint32>& pool = byQuality[quality];
            std::shuffle(pool.begin(), pool.end(), rng);
            pool.resize(std::min<size_t>(pool.size(), DAILY_BY_QUALITY[quality]));
            std::sort(pool.begin(), pool.end());
            picked.insert(picked.end(), pool.begin(), pool.end());
        }
        return picked;
    }

    bool VendorHasGoods(uint32 entry)
    {
        VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(entry);
        if (!items || items->GetItemCount() != goods.size())
            return false;
        for (uint32 slot = 0; slot < goods.size(); ++slot)
            if (items->GetItem(slot)->item != goods[slot])
                return false;
        return true;
    }

    void Update()
    {
        time_t day = Acore::Time::GetLocalHourTimestamp(GameTime::GetGameTime().count(), 0, false);
        bool newDay = day != goodsDay;
        if (newDay)
        {
            goods = PickGoods(day);
            goodsDay = day;
        }
        for (uint32 entry : VENDORS)
        {
            if (!sObjectMgr->GetCreatureTemplate(entry) || VendorHasGoods(entry))
                continue;
            if (VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(entry))
            {
                std::vector<uint32> old;
                for (uint32 slot = 0; slot < items->GetItemCount(); ++slot)
                    old.push_back(items->GetItem(slot)->item);
                for (uint32 item : old)
                    sObjectMgr->RemoveVendorItem(entry, item, false);
            }
            for (uint32 item : goods)
                sObjectMgr->AddVendorItem(entry, item, 0, 0, 0, false);
        }
        if (newDay)
            LOG_INFO("server.loading", ">> XorWoW TCG: the Card Vendor sells {} cards today", goods.size());
    }
}

class xorwow_tcg_vendor_worldscript : public WorldScript
{
public:
    xorwow_tcg_vendor_worldscript() : WorldScript("xorwow_tcg_vendor_worldscript", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }

    void OnStartup() override
    {
        LoadCardItems();
        Update();
    }

    void OnUpdate(uint32 diff) override
    {
        timer += diff;
        if (timer < CHECK_MS)
            return;
        timer = 0;
        Update();
    }

private:
    uint32 timer = 0;
};

void AddSC_xorwow_tcg_vendor()
{
    new xorwow_tcg_vendor_worldscript();
}
