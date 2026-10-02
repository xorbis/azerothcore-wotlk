-- XorWoW: the Guildstone (item 260001), 10 gold at the guild tabard vendors; on use it casts
-- Guild Hall teleportation (spell 260001, src/server/scripts/Custom/xorwow_guildstone.cpp).
--
-- The item is a copy of the Hearthstone (6948): class 15, bind on pickup, unique, the use spell
-- cast by the player. Its icon, display (ItemDisplayInfo 260001), Item.dbc row and the spell come
-- from the client patch (Patch-Z.MPQ); their server rows are in 2026_10_02_00_xorwow_client_patch_dbc.sql.
-- Idempotent.
DELETE FROM `item_template` WHERE `entry` = 260001;
DROP TEMPORARY TABLE IF EXISTS `xorwow_guildstone`;
CREATE TEMPORARY TABLE `xorwow_guildstone` SELECT * FROM `item_template` WHERE `entry` = 6948;
UPDATE `xorwow_guildstone` SET
    `entry` = 260001, `name` = 'Guildstone', `displayid` = 260001,
    `BuyCount` = 1, `BuyPrice` = 100000, `SellPrice` = 0,
    `spellid_1` = 260001, `spelltrigger_1` = 0, `spellcharges_1` = 0,
    `spellcooldown_1` = -1, `spellcategory_1` = 0, `spellcategorycooldown_1` = -1,
    `description` = '', `ScriptName` = '';
INSERT INTO `item_template` SELECT * FROM `xorwow_guildstone`;
DROP TEMPORARY TABLE `xorwow_guildstone`;
DELETE FROM `item_template_locale` WHERE `ID` = 260001;

-- Every vendor that sells the Guild Tabard (5976): Stormwind, Orgrimmar, Thunder Bluff, Undercity (two).
DELETE FROM `npc_vendor` WHERE `item` = 260001;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`)
SELECT DISTINCT `entry`, 0, 260001, 0, 0, 0 FROM `npc_vendor` WHERE `item` = 5976;

DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_xorwow_guild_hall_teleport';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (260001, 'spell_xorwow_guild_hall_teleport');
