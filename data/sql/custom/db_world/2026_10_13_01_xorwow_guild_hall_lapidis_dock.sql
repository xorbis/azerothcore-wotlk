-- XorWoW: in the Island of Dr Lapidis guild hall, a ghost released inside appears beside its Spirit
-- Healer (2026_10_13_02_xorwow_guild_hall_lapidis_spirit_healer.sql), not at the entrance on the
-- dock (user, 2026-10-13). 2026_10_13_00_xorwow_guild_hall_isles.sql already has the new spot;
-- this updates databases that applied it before. Idempotent.
UPDATE `game_graveyard` SET `x` = -11908.1, `y` = 3301.32, `z` = 23.8036 WHERE `ID` IN (1727, 1728);
