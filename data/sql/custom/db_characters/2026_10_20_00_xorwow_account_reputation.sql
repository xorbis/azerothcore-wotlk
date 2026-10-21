-- XorWoW: reputation is shared by the account (src/server/scripts/Custom/xorwow_account_reputation.cpp).
-- The standing the account's characters have earned with each faction, above their race/class base
-- (the character_reputation.standing format). side 0 = shared by the whole account, 1 = Alliance
-- characters only, 2 = Horde characters only (factions that start at Hated for the other side).
CREATE TABLE IF NOT EXISTS `xorwow_account_reputation` (
  `account` INT UNSIGNED NOT NULL,
  `side` TINYINT UNSIGNED NOT NULL,
  `faction` SMALLINT UNSIGNED NOT NULL,
  `standing` INT NOT NULL,
  PRIMARY KEY (`account`, `side`, `faction`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: earned reputation per account';
