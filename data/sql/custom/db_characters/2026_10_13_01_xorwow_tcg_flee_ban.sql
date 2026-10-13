-- XorWoW TCG (xorwow_tcg.cpp): leaving 3 games in one day bars the account from playing for 3 days.
ALTER TABLE `xorwow_tcg_account`
  ADD COLUMN `banned_until` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time the account may play again after leaving too many games' AFTER `bot_fled`;
