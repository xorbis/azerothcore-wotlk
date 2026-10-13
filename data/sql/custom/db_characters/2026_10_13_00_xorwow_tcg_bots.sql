-- XorWoW TCG against the world's bots (xorwow_tcg.cpp): each bot's strategy, and the games against
-- bots kept apart from the games against players.
CREATE TABLE IF NOT EXISTS `xorwow_tcg_bot` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'the bot character',
  `strategy` TINYINT UNSIGNED NOT NULL COMMENT '0 Bad, 1 Poor, 2 Medium, 3 Strong, 4 Perfect',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW TCG: how well each bot plays, rolled once per bot';

ALTER TABLE `xorwow_tcg_account`
  ADD COLUMN `bot_wins` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'games against bots, apart from the record above' AFTER `feats`,
  ADD COLUMN `bot_losses` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `bot_wins`,
  ADD COLUMN `bot_draws` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `bot_losses`,
  ADD COLUMN `bot_fled` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `bot_draws`;

ALTER TABLE `xorwow_tcg_history`
  ADD COLUMN `bot` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 a player, else a bot: its strategy + 1' AFTER `time`,
  ADD KEY `account_bot` (`account`, `bot`, `id`);
