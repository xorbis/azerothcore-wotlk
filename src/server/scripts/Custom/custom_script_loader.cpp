/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// This is where scripts' loading functions should be declared:
// void MyExampleScript()
void AddSC_xorwow_guild_bank_tab();   // ".guild buytab" - guild bank tabs at the realm's price
void AddSC_xorwow_engineering_arrows();   // engineering bullet recipes make arrows too, ".ammo"
void AddSC_xorwow_stable_slot();   // ".stable buyslot" - stable slots at the realm's price
void AddSC_xorwow_gather_nodes();   // tells the XorWoW addon when a tracked herb or ore reaches the minimap
void AddSC_xorwow_expansion_lock();   // Outland and Northrend closed while the level cap is 60
void AddSC_xorwow_radio();   // the song title for the XorWoW addon's radio button
void AddSC_xorwow_guild_hall();   // the Guildstone's teleport, the "Edit Guild Hall" rank toggle
void AddSC_xorwow_guild_hall_build();   // guild hall build mode: objects placed, moved and refunded
void AddSC_xorwow_guild_epic_loot();   // guild chat announces a guild member's epic loot

// The name of this function should match:
// void Add${NameOfDirectory}Scripts()
void AddCustomScripts()
{
    // MyExampleScript()
    AddSC_xorwow_guild_bank_tab();
    AddSC_xorwow_engineering_arrows();
    AddSC_xorwow_stable_slot();
    AddSC_xorwow_gather_nodes();
    AddSC_xorwow_expansion_lock();
    AddSC_xorwow_radio();
    AddSC_xorwow_guild_hall();
    AddSC_xorwow_guild_hall_build();
    AddSC_xorwow_guild_epic_loot();
}
