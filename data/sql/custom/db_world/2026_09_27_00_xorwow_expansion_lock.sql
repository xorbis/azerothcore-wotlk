-- XorWoW: Burning Crusade and Wrath content closed while the realm is capped at level 60
-- (MaxPlayerLevel = 60, 2026-09-27, until further notice). Travel into Outland/Northrend by
-- teleport is refused by src/server/scripts/Custom/xorwow_expansion_lock.cpp; this file is the
-- data half. Read at worldserver startup (restart, not .reload). Idempotent.
--
-- 1. Every Burning Crusade and Wrath dungeon and raid is disabled (sourceType 2 = map, flags =
--    every difficulty the map has): the dungeon finder lists them as locked and their entrances
--    say "instance closed". GMs bypass it by permission.
DELETE FROM `disables` WHERE `sourceType` = 2 AND `comment` LIKE 'XorWoW level cap 60:%';
INSERT INTO `disables` (`sourceType`, `entry`, `flags`, `params_0`, `params_1`, `comment`) VALUES
(2, 269, 3, '', '', 'XorWoW level cap 60: Opening of the Dark Portal'),
(2, 532, 1, '', '', 'XorWoW level cap 60: Karazhan'),
(2, 533, 3, '', '', 'XorWoW level cap 60: Naxxramas'),
(2, 534, 1, '', '', 'XorWoW level cap 60: The Battle for Mount Hyjal'),
(2, 540, 3, '', '', 'XorWoW level cap 60: Hellfire Citadel: The Shattered Halls'),
(2, 542, 3, '', '', 'XorWoW level cap 60: Hellfire Citadel: The Blood Furnace'),
(2, 543, 3, '', '', 'XorWoW level cap 60: Hellfire Citadel: Ramparts'),
(2, 544, 1, '', '', 'XorWoW level cap 60: Magtheridon''s Lair'),
(2, 545, 3, '', '', 'XorWoW level cap 60: Coilfang: The Steamvault'),
(2, 546, 3, '', '', 'XorWoW level cap 60: Coilfang: The Underbog'),
(2, 547, 3, '', '', 'XorWoW level cap 60: Coilfang: The Slave Pens'),
(2, 548, 1, '', '', 'XorWoW level cap 60: Coilfang: Serpentshrine Cavern'),
(2, 550, 1, '', '', 'XorWoW level cap 60: Tempest Keep'),
(2, 552, 3, '', '', 'XorWoW level cap 60: Tempest Keep: The Arcatraz'),
(2, 553, 3, '', '', 'XorWoW level cap 60: Tempest Keep: The Botanica'),
(2, 554, 3, '', '', 'XorWoW level cap 60: Tempest Keep: The Mechanar'),
(2, 555, 3, '', '', 'XorWoW level cap 60: Auchindoun: Shadow Labyrinth'),
(2, 556, 3, '', '', 'XorWoW level cap 60: Auchindoun: Sethekk Halls'),
(2, 557, 3, '', '', 'XorWoW level cap 60: Auchindoun: Mana-Tombs'),
(2, 558, 3, '', '', 'XorWoW level cap 60: Auchindoun: Auchenai Crypts'),
(2, 560, 3, '', '', 'XorWoW level cap 60: The Escape From Durnholde'),
(2, 564, 1, '', '', 'XorWoW level cap 60: Black Temple'),
(2, 565, 1, '', '', 'XorWoW level cap 60: Gruul''s Lair'),
(2, 568, 1, '', '', 'XorWoW level cap 60: Zul''Aman'),
(2, 574, 3, '', '', 'XorWoW level cap 60: Utgarde Keep'),
(2, 575, 3, '', '', 'XorWoW level cap 60: Utgarde Pinnacle'),
(2, 576, 3, '', '', 'XorWoW level cap 60: The Nexus'),
(2, 578, 3, '', '', 'XorWoW level cap 60: The Oculus'),
(2, 580, 1, '', '', 'XorWoW level cap 60: The Sunwell'),
(2, 585, 3, '', '', 'XorWoW level cap 60: Magister''s Terrace'),
(2, 595, 3, '', '', 'XorWoW level cap 60: The Culling of Stratholme'),
(2, 599, 3, '', '', 'XorWoW level cap 60: Halls of Stone'),
(2, 600, 3, '', '', 'XorWoW level cap 60: Drak''Tharon Keep'),
(2, 601, 3, '', '', 'XorWoW level cap 60: Azjol-Nerub'),
(2, 602, 3, '', '', 'XorWoW level cap 60: Halls of Lightning'),
(2, 603, 3, '', '', 'XorWoW level cap 60: Ulduar'),
(2, 604, 3, '', '', 'XorWoW level cap 60: Gundrak'),
(2, 608, 3, '', '', 'XorWoW level cap 60: Violet Hold'),
(2, 615, 3, '', '', 'XorWoW level cap 60: The Obsidian Sanctum'),
(2, 616, 3, '', '', 'XorWoW level cap 60: The Eye of Eternity'),
(2, 619, 3, '', '', 'XorWoW level cap 60: Ahn''kahet: The Old Kingdom'),
(2, 624, 3, '', '', 'XorWoW level cap 60: Vault of Archavon'),
(2, 631, 15, '', '', 'XorWoW level cap 60: Icecrown Citadel'),
(2, 632, 3, '', '', 'XorWoW level cap 60: The Forge of Souls'),
(2, 649, 15, '', '', 'XorWoW level cap 60: Trial of the Crusader'),
(2, 650, 3, '', '', 'XorWoW level cap 60: Trial of the Champion'),
(2, 658, 3, '', '', 'XorWoW level cap 60: Pit of Saron'),
(2, 668, 3, '', '', 'XorWoW level cap 60: Halls of Reflection'),
(2, 724, 15, '', '', 'XorWoW level cap 60: The Ruby Sanctum');

-- 2. No boats or zeppelins to Northrend (Menethil - Valgarde, Undercity - Vengeance Landing,
--    Orgrimmar - Warsong Hold, Stormwind - Valiance Keep). The docks stay empty.
DELETE FROM `transports` WHERE `guid` IN (10, 11, 12, 17);

-- Undo (reopening the expansions): run these, remove AddSC_xorwow_expansion_lock() from
-- custom_script_loader.cpp, and delete this file so the updater does not apply it again.
-- DELETE FROM `disables` WHERE `sourceType` = 2 AND `comment` LIKE 'XorWoW level cap 60:%';
-- INSERT INTO `transports` (`guid`, `entry`, `name`, `ScriptName`) VALUES
-- (10, 181688, 'Menethil Harbor, Wetlands and Valgarde, Howling Fjord (Boat, Alliance ("Northspear"))', ''),
-- (11, 181689, 'Undercity, Tirisfal Glades and Vengeance Landing, Howling Fjord (Zeppelin, Horde ("Cloudkisser"))', ''),
-- (12, 186238, 'Orgrimmar, Durotar and Warsong Hold, Borean Tundra (Zeppelin, Horde ("The Mighty Wind"))', ''),
-- (17, 190536, 'Valiance Keep, Borean Tundra and Stormwind Harbor (Boat, Alliance ("The Kraken"))', '');
