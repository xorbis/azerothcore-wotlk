-- XorWoW TCG achievements (xorwow_tcg.cpp): what the record alone cannot tell, per account. Every
-- character of the account is given the achievements these earn when its addon says hello.
ALTER TABLE `xorwow_tcg_account`
  ADD COLUMN `streak` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'wins in a row now (a loss, a draw or fleeing ends it)' AFTER `cards_lost`,
  ADD COLUMN `best_streak` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the longest run of wins' AFTER `streak`,
  ADD COLUMN `feats` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'one bit per one-time feat (Feat in xorwow_tcg.cpp)' AFTER `best_streak`;
