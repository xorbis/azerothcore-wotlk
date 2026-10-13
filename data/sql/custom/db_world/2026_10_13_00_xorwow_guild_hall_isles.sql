-- XorWoW: two more guild halls a guild master can pick (src/server/scripts/Custom/xorwow_guild_hall.cpp):
-- the Island of Dr Lapidis as 731/732 and Gillijim's Isle as 733/734, Alliance/Horde - islands
-- off Stranglethorn made in Noggit by "Nicolas" (wowmodding.net file 131), maps of their own.
-- Their Map, MapDifficulty, AreaTable and Light rows (areas 4994-4997, "Guild Hall", capital
-- flags) are in 2026_10_02_00_xorwow_client_patch_dbc.sql, their terrain/collision/pathing files
-- come from scripts\extract-server-maps.ps1 in the XorWoW repo. Idempotent.

-- Dungeon maps need an instance template; mounts allowed, as in a city.
DELETE FROM `instance_template` WHERE `map` IN (731, 732, 733, 734);
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES
(731, 0, '', 1),
(732, 0, '', 1),
(733, 0, '', 1),
(734, 0, '', 1);

-- Where a ghost appears after releasing inside the hall: the entrance - the Guildstone's landing
-- spot - except on the Island of Dr Lapidis, where it is the Spirit Healer (user, 2026-10-13).
DELETE FROM `game_graveyard` WHERE `ID` IN (1727, 1728, 1729, 1730);
INSERT INTO `game_graveyard` (`ID`, `Map`, `x`, `y`, `z`, `Comment`) VALUES
(1727, 731, -11908.1, 3301.32, 23.8036, 'XorWoW Alliance Guild Hall (Island of Dr Lapidis) - entrance'),
(1728, 732, -11908.1, 3301.32, 23.8036, 'XorWoW Horde Guild Hall (Island of Dr Lapidis) - entrance'),
(1729, 733, -13925.0, 3212.5, 19.0, 'XorWoW Alliance Guild Hall (Gillijim''s Isle) - entrance'),
(1730, 734, -13925.0, 3212.5, 19.0, 'XorWoW Horde Guild Hall (Gillijim''s Isle) - entrance');

DELETE FROM `graveyard_zone` WHERE `ID` IN (1727, 1728, 1729, 1730);
INSERT INTO `graveyard_zone` (`ID`, `GhostZone`, `Faction`, `Comment`) VALUES
(1727, 4994, 469, 'XorWoW Alliance Guild Hall (Island of Dr Lapidis)'),
(1728, 4995, 67, 'XorWoW Horde Guild Hall (Island of Dr Lapidis)'),
(1729, 4996, 469, 'XorWoW Alliance Guild Hall (Gillijim''s Isle)'),
(1730, 4997, 67, 'XorWoW Horde Guild Hall (Gillijim''s Isle)');

-- Their DBC rows for the server (map_dbc, mapdifficulty_dbc, areatable_dbc), the same as in the
-- generated 2026_10_02_00_xorwow_client_patch_dbc.sql: the database import does not re-apply a file
-- that changed, so a new file carries them.
DELETE FROM `map_dbc` WHERE `ID` IN (731, 732, 733, 734);
INSERT INTO `map_dbc` (`ID`, `Directory`, `InstanceType`, `Flags`, `PVP`, `MapName_Lang_enUS`, `MapName_Lang_enGB`, `MapName_Lang_koKR`, `MapName_Lang_frFR`, `MapName_Lang_deDE`, `MapName_Lang_enCN`, `MapName_Lang_zhCN`, `MapName_Lang_enTW`, `MapName_Lang_zhTW`, `MapName_Lang_esES`, `MapName_Lang_esMX`, `MapName_Lang_ruRU`, `MapName_Lang_ptPT`, `MapName_Lang_ptBR`, `MapName_Lang_itIT`, `MapName_Lang_Unk`, `MapName_Lang_Mask`, `AreaTableID`, `MapDescription0_Lang_enUS`, `MapDescription0_Lang_enGB`, `MapDescription0_Lang_koKR`, `MapDescription0_Lang_frFR`, `MapDescription0_Lang_deDE`, `MapDescription0_Lang_enCN`, `MapDescription0_Lang_zhCN`, `MapDescription0_Lang_enTW`, `MapDescription0_Lang_zhTW`, `MapDescription0_Lang_esES`, `MapDescription0_Lang_esMX`, `MapDescription0_Lang_ruRU`, `MapDescription0_Lang_ptPT`, `MapDescription0_Lang_ptBR`, `MapDescription0_Lang_itIT`, `MapDescription0_Lang_Unk`, `MapDescription0_Lang_Mask`, `MapDescription1_Lang_enUS`, `MapDescription1_Lang_enGB`, `MapDescription1_Lang_koKR`, `MapDescription1_Lang_frFR`, `MapDescription1_Lang_deDE`, `MapDescription1_Lang_enCN`, `MapDescription1_Lang_zhCN`, `MapDescription1_Lang_enTW`, `MapDescription1_Lang_zhTW`, `MapDescription1_Lang_esES`, `MapDescription1_Lang_esMX`, `MapDescription1_Lang_ruRU`, `MapDescription1_Lang_ptPT`, `MapDescription1_Lang_ptBR`, `MapDescription1_Lang_itIT`, `MapDescription1_Lang_Unk`, `MapDescription1_Lang_Mask`, `LoadingScreenID`, `MinimapIconScale`, `CorpseMapID`, `CorpseX`, `CorpseY`, `TimeOfDayOverride`, `ExpansionID`, `RaidOffset`, `MaxPlayers`) VALUES
(731, 'XorWoWGuildHallLapidisAlliance', 1, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 4994, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 255, 1.0, -1, 5689.48974609375, 498.04693603515625, -1, 0, 0, 100),
(732, 'XorWoWGuildHallLapidisHorde', 1, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 4995, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 256, 1.0, -1, 5689.48974609375, 498.04693603515625, -1, 0, 0, 100),
(733, 'XorWoWGuildHallGillijimAlliance', 1, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 4996, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 255, 1.0, -1, 5689.48974609375, 498.04693603515625, -1, 0, 0, 100),
(734, 'XorWoWGuildHallGillijimHorde', 1, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 4997, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 256, 1.0, -1, 5689.48974609375, 498.04693603515625, -1, 0, 0, 100);
DELETE FROM `mapdifficulty_dbc` WHERE `ID` IN (760, 761, 762, 763);
INSERT INTO `mapdifficulty_dbc` (`ID`, `MapID`, `Difficulty`, `Message_Lang_enUS`, `Message_Lang_enGB`, `Message_Lang_koKR`, `Message_Lang_frFR`, `Message_Lang_deDE`, `Message_Lang_enCN`, `Message_Lang_zhCN`, `Message_Lang_enTW`, `Message_Lang_zhTW`, `Message_Lang_esES`, `Message_Lang_esMX`, `Message_Lang_ruRU`, `Message_Lang_ptPT`, `Message_Lang_ptBR`, `Message_Lang_itIT`, `Message_Lang_Unk`, `Message_Lang_Mask`, `RaidDuration`, `MaxPlayers`, `Difficultystring`) VALUES
(760, 731, 0, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 0, 100, ''),
(761, 732, 0, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 0, 100, ''),
(762, 733, 0, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 0, 100, ''),
(763, 734, 0, '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712188, 0, 100, '');
DELETE FROM `areatable_dbc` WHERE `ID` IN (4994, 4995, 4996, 4997);
INSERT INTO `areatable_dbc` (`ID`, `ContinentID`, `ParentAreaID`, `AreaBit`, `Flags`, `SoundProviderPref`, `SoundProviderPrefUnderwater`, `AmbienceID`, `ZoneMusic`, `IntroSound`, `ExplorationLevel`, `AreaName_Lang_enUS`, `AreaName_Lang_enGB`, `AreaName_Lang_koKR`, `AreaName_Lang_frFR`, `AreaName_Lang_deDE`, `AreaName_Lang_enCN`, `AreaName_Lang_zhCN`, `AreaName_Lang_enTW`, `AreaName_Lang_zhTW`, `AreaName_Lang_esES`, `AreaName_Lang_esMX`, `AreaName_Lang_ruRU`, `AreaName_Lang_ptPT`, `AreaName_Lang_ptBR`, `AreaName_Lang_itIT`, `AreaName_Lang_Unk`, `AreaName_Lang_Mask`, `FactionGroupMask`, `LiquidTypeID_1`, `LiquidTypeID_2`, `LiquidTypeID_3`, `LiquidTypeID_4`, `MinElevation`, `Ambient_Multiplier`, `Lightid`) VALUES
(4994, 731, 0, 2119, 69206328, 0, 0, 0, 0, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 2, 0, 0, 0, 0, -500.0, 0.0, 0),
(4995, 732, 0, 2119, 69206328, 0, 0, 0, 0, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 4, 0, 0, 0, 0, -500.0, 0.0, 0),
(4996, 733, 0, 2119, 69206328, 0, 0, 0, 0, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 2, 0, 0, 0, 0, -500.0, 0.0, 0),
(4997, 734, 0, 2119, 69206328, 0, 0, 0, 0, 0, 0, 'Guild Hall', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 16712190, 4, 0, 0, 0, 0, -500.0, 0.0, 0);
