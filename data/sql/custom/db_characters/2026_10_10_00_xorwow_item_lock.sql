-- XorWoW item locks (src/server/scripts/Custom/xorwow_item_lock.cpp): items a character locked in
-- its bags or bank, which no vendor will buy until unlocked.
CREATE TABLE IF NOT EXISTS `xorwow_item_lock` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  `item` INT UNSIGNED NOT NULL COMMENT 'item_instance.guid',
  PRIMARY KEY (`item`),
  KEY `owner` (`owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='XorWoW: items locked against vendor sales';
