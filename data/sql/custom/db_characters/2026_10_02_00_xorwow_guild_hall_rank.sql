-- XorWoW: the guild ranks allowed to edit their guild hall (src/server/scripts/Custom/xorwow_guild_hall.cpp).
-- One row per allowed rank; the guild master's rank (0) always may and has no row. Set from the
-- "Edit Guild Hall" checkbox the XorWoW addon adds to the Guild Control window.
CREATE TABLE IF NOT EXISTS `xorwow_guild_hall_rank` (
  `guildid` INT UNSIGNED NOT NULL,
  `rid` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`guildid`, `rid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: guild ranks that may edit the guild hall';
