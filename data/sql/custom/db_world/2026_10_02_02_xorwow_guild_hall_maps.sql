-- XorWoW: the guild hall maps (src/server/scripts/Custom/xorwow_guild_hall.cpp): 725 for the
-- Alliance, 726 for the Horde, both copies of Dalaran Sewers (617). Their Map, MapDifficulty and
-- AreaTable rows (areas 4988/4989, "Guild Hall", capital flags) are in
-- 2026_10_02_00_xorwow_client_patch_dbc.sql, their terrain/collision/pathing files are copied into
-- the client-data volume by scripts\build-patch.ps1 in the XorWoW repo. Idempotent.

-- Dungeon maps need an instance template; mounts allowed, as in a city.
DELETE FROM `instance_template` WHERE `map` IN (725, 726);
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES
(725, 0, '', 1),
(726, 0, '', 1);

-- The entrance - Dalaran Sewers' team start points, the Guildstone's landing spot - is also where
-- a ghost appears after releasing inside the hall.
DELETE FROM `game_graveyard` WHERE `ID` IN (1721, 1722);
INSERT INTO `game_graveyard` (`ID`, `Map`, `x`, `y`, `z`, `Comment`) VALUES
(1721, 725, 1218.01, 764.795, 14.7297, 'XorWoW Alliance Guild Hall - entrance'),
(1722, 726, 1361.76, 817.337, 14.8449, 'XorWoW Horde Guild Hall - entrance');

DELETE FROM `graveyard_zone` WHERE `ID` IN (1721, 1722);
INSERT INTO `graveyard_zone` (`ID`, `GhostZone`, `Faction`, `Comment`) VALUES
(1721, 4988, 469, 'XorWoW Alliance Guild Hall'),
(1722, 4989, 67, 'XorWoW Horde Guild Hall');
