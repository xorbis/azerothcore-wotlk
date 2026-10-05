-- XorWoW: the guild halls a guild master can pick besides Dalaran Sewers
-- (src/server/scripts/Custom/xorwow_guild_hall.cpp): copies of Nagrand Arena (559) as 727/728 and
-- the Violet Hold (608) as 729/730, Alliance/Horde.
-- Their Map, MapDifficulty and AreaTable rows (areas 4990-4993, "Guild Hall", capital flags) are in
-- 2026_10_02_00_xorwow_client_patch_dbc.sql, their terrain/collision/pathing files are copied into
-- the client-data volume by scripts\build-patch.ps1 in the XorWoW repo. Idempotent.

-- Dungeon maps need an instance template; mounts allowed, as in a city. No instance script: none
-- of the source dungeons' events run in a hall.
DELETE FROM `instance_template` WHERE `map` IN (727, 728, 729, 730);
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES
(727, 0, '', 1),
(728, 0, '', 1),
(729, 0, '', 1),
(730, 0, '', 1);

-- The entrance - the Guildstone's landing spot - is also where a ghost appears after releasing
-- inside the hall: Nagrand Arena's team start points, the Violet Hold's own entrance (its
-- areatrigger_teleport target).
DELETE FROM `game_graveyard` WHERE `ID` IN (1723, 1724, 1725, 1726);
INSERT INTO `game_graveyard` (`ID`, `Map`, `x`, `y`, `z`, `Comment`) VALUES
(1723, 727, 4027.6, 2972.78, 12.0723, 'XorWoW Alliance Guild Hall (Nagrand Arena) - entrance'),
(1724, 728, 4085.45, 2866.83, 12.4005, 'XorWoW Horde Guild Hall (Nagrand Arena) - entrance'),
(1725, 729, 1808.82, 803.93, 44.364, 'XorWoW Alliance Guild Hall (Violet Hold) - entrance'),
(1726, 730, 1808.82, 803.93, 44.364, 'XorWoW Horde Guild Hall (Violet Hold) - entrance');

DELETE FROM `graveyard_zone` WHERE `ID` IN (1723, 1724, 1725, 1726);
INSERT INTO `graveyard_zone` (`ID`, `GhostZone`, `Faction`, `Comment`) VALUES
(1723, 4990, 469, 'XorWoW Alliance Guild Hall (Nagrand Arena)'),
(1724, 4991, 67, 'XorWoW Horde Guild Hall (Nagrand Arena)'),
(1725, 4992, 469, 'XorWoW Alliance Guild Hall (Violet Hold)'),
(1726, 4993, 67, 'XorWoW Horde Guild Hall (Violet Hold)');
