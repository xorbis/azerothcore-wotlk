-- XorWoW: the Island of Dr Lapidis guild hall's entrance moves to its dock (user, 2026-10-13) - where
-- the Guildstone lands and a ghost released inside reappears (xorwow_guild_hall.cpp HALLS).
-- 2026_10_13_00_xorwow_guild_hall_isles.sql already has the new spot; this updates databases that
-- applied it before. Idempotent.
UPDATE `game_graveyard` SET `x` = -11876.7, `y` = 3104.04, `z` = 10.2148 WHERE `ID` IN (1727, 1728);
