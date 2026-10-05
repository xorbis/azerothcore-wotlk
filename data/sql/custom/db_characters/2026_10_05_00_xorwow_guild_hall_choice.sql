-- XorWoW: which hall each guild uses (src/server/scripts/Custom/xorwow_guild_hall.cpp), picked by
-- its guild master in the build panel. hall: 0 Dalaran Sewers, 1 Nagrand Arena, 2 Violet Hold.
-- No row = Dalaran Sewers.
CREATE TABLE IF NOT EXISTS `xorwow_guild_hall_choice` (
  `guildid` INT UNSIGNED NOT NULL,
  `hall` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`guildid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: the guild hall each guild uses';
