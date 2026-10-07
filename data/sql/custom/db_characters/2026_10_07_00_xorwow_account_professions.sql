-- XorWoW: account-bound gathering and secondary professions (src/server/scripts/Custom/xorwow_account_professions.cpp).
-- Mining, Herbalism, Skinning, First Aid, Cooking and Fishing: the best skill value and every rank
-- spell and recipe any character of the account has had. A character that learns one of these
-- professions gets the account's progress in it; dropping it loses nothing.
CREATE TABLE IF NOT EXISTS `xorwow_account_profession_skill` (
  `account` INT UNSIGNED NOT NULL,
  `skill` SMALLINT UNSIGNED NOT NULL,
  `value` SMALLINT UNSIGNED NOT NULL,
  PRIMARY KEY (`account`, `skill`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: best skill value per account in the shared professions';

CREATE TABLE IF NOT EXISTS `xorwow_account_profession_spell` (
  `account` INT UNSIGNED NOT NULL,
  `spell` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: rank spells and recipes per account in the shared professions';
