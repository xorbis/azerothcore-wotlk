-- XorWoW: what each guild placed in its guild hall (src/server/scripts/Custom/xorwow_guild_hall_build.cpp).
-- item is world.xorwow_guild_hall_catalog.id; price is what the guild bank paid, which a refund gives back.
CREATE TABLE IF NOT EXISTS `xorwow_guild_hall_object` (
  `id` INT UNSIGNED NOT NULL,
  `guildid` INT UNSIGNED NOT NULL,
  `map` SMALLINT UNSIGNED NOT NULL,
  `item` INT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL,
  `y` FLOAT NOT NULL,
  `z` FLOAT NOT NULL,
  `o` FLOAT NOT NULL,
  `scale` SMALLINT UNSIGNED NOT NULL DEFAULT 100 COMMENT 'percent',
  `price` INT UNSIGNED NOT NULL COMMENT 'copper',
  `placed_by` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  `placed_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`id`),
  KEY `guildid` (`guildid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: guild hall objects';
