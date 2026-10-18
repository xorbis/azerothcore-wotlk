-- XorWoW: flight paths are shared by the account (src/server/scripts/Custom/xorwow_account_taxi.cpp).
-- Every flight path any character of the account has discovered, in the characters.taximask format
-- (14 space-separated uint32). Each character gets the ones its faction can fly at login.
CREATE TABLE IF NOT EXISTS `xorwow_account_taxi` (
  `account` INT UNSIGNED NOT NULL,
  `taximask` TEXT NOT NULL,
  PRIMARY KEY (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: flight paths per account';
