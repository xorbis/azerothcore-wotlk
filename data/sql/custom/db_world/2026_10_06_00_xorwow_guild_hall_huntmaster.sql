-- Guild hall Huntmasters (catalogue items 398 Alliance / 399 Horde, creatures 260398 / 260399, copies
-- of mod-hunts' Stormwind and Orgrimmar Huntmasters, made by 2026_10_02_03_xorwow_guild_hall_catalog.sql).
-- Continent 0: mod-hunts (xorwow fork) gives hunts on every continent the level cap has opened.
DELETE FROM `hunt_giver` WHERE `id` IN (11, 12);
INSERT INTO `hunt_giver` (`id`, `creature_entry`, `city_name`, `map_id`, `continent_id`, `x`, `y`, `z`, `enabled`, `comment`) VALUES
(11, 260398, 'Guild Hall', 725, 0, 0, 0, 0, 1, 'XorWoW guild hall Huntmaster (Alliance)'),
(12, 260399, 'Guild Hall', 726, 0, 0, 0, 0, 1, 'XorWoW guild hall Huntmaster (Horde)');
