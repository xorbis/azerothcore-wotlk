-- XorWoW: explored zones are shared by the account (src/server/scripts/Custom/xorwow_account_exploration.cpp).
-- The union of every character's PLAYER_EXPLORED_ZONES bits, in the characters.exploredZones format
-- (128 space-separated uint32). Each character of the account gets this map at login.
CREATE TABLE IF NOT EXISTS `xorwow_account_explored` (
  `account` INT UNSIGNED NOT NULL,
  `explored` LONGTEXT NOT NULL,
  PRIMARY KEY (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: explored zones per account';
