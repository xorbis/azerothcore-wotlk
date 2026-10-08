-- XorWoW TCG (src/server/scripts/Custom/xorwow_tcg.cpp): each account's cards, deck, record and
-- last games. The cards themselves are in the world database (xorwow_tcg_card).
CREATE TABLE IF NOT EXISTS `xorwow_tcg_collection` (
  `account` INT UNSIGNED NOT NULL,
  `card` TINYINT UNSIGNED NOT NULL,
  `count` SMALLINT UNSIGNED NOT NULL,
  PRIMARY KEY (`account`, `card`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW TCG: copies of each card an account owns';

CREATE TABLE IF NOT EXISTS `xorwow_tcg_account` (
  `account` INT UNSIGNED NOT NULL,
  `deck` VARCHAR(32) NOT NULL DEFAULT '' COMMENT 'the 5 card ids of the saved deck, comma separated',
  `wins` INT UNSIGNED NOT NULL DEFAULT 0,
  `losses` INT UNSIGNED NOT NULL DEFAULT 0,
  `draws` INT UNSIGNED NOT NULL DEFAULT 0,
  `fled` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'games lost by leaving: out of range, dead, teleported, zoned or logged out',
  `cards_won` INT UNSIGNED NOT NULL DEFAULT 0,
  `cards_lost` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW TCG: deck and record per account (the row also marks the starter deck as given)';

CREATE TABLE IF NOT EXISTS `xorwow_tcg_history` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account` INT UNSIGNED NOT NULL,
  `opponent` VARCHAR(12) NOT NULL,
  `result` TINYINT UNSIGNED NOT NULL COMMENT '0 loss, 1 win, 2 draw, 3 fled (loss), 4 opponent fled (win)',
  `score` TINYINT UNSIGNED NOT NULL,
  `opponent_score` TINYINT UNSIGNED NOT NULL,
  `stakes` VARCHAR(100) NOT NULL DEFAULT '',
  `place` VARCHAR(64) NOT NULL DEFAULT '',
  `time` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`id`),
  KEY `account` (`account`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW TCG: games played, per account';
