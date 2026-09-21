-- XorWoW: Dual Talent Specialization costs 100 gold instead of the stock 1000 (BoxMoney is in
-- copper; the confirmation popup shows whatever is stored here). OptionType 18 is
-- GOSSIP_OPTION_LEARNDUALSPEC - the "Purchase a Dual Talent Specialization" option of every class
-- trainer (menus 0, 6647 and 10371). The level requirement is not in the DB: MinDualSpecLevel in
-- worldserver.conf (30 on XorWoW). Idempotent.
UPDATE `gossip_menu_option` SET `BoxMoney` = 1000000 WHERE `OptionType` = 18;
