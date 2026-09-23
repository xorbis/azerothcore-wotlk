-- XorWoW: engineering bullet recipes make arrows too (src/server/scripts/Custom/xorwow_engineering_arrows.cpp).
--
-- The script is bound to the seven bullet recipes: Crafted Light/Heavy/Solid Shot, Hi-Impact
-- Mithril Slugs, Mithril Gyro-Shot, Thorium Shells, Fel Iron Shells.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_xorwow_engineering_arrows';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(3920,  'spell_xorwow_engineering_arrows'),
(3930,  'spell_xorwow_engineering_arrows'),
(3947,  'spell_xorwow_engineering_arrows'),
(12596, 'spell_xorwow_engineering_arrows'),
(12621, 'spell_xorwow_engineering_arrows'),
(19800, 'spell_xorwow_engineering_arrows'),
(30346, 'spell_xorwow_engineering_arrows');

-- Two bullets have no arrow twin: Mithril Gyro-Shot, and Fel Iron Shells (Scout's Arrow has its
-- damage but is bind-on-pickup and needs level 61). Their arrows reuse two deprecated arrow entries
-- that nothing drops, sells or rewards: the client's Item.dbc already lists them as arrows (class 6,
-- subclass 2, ammo slot), so no client patch is needed, and everything else a client shows about an
-- item comes from the server. Each copies its bullet's numbers; arrows go in quivers (BagFamily 1).
-- The display stays 5996, the one the client's Item.dbc has for them. Idempotent.
UPDATE `item_template` SET
    `name` = 'Mithril Gyro-Arrow', `Quality` = 2, `Flags` = 0, `FlagsExtra` = 0,
    `BuyCount` = 200, `BuyPrice` = 2000, `SellPrice` = 5, `ItemLevel` = 49, `RequiredLevel` = 44,
    `stackable` = 1000, `dmg_min1` = 15, `dmg_max1` = 15, `delay` = 3000, `BagFamily` = 1
WHERE `entry` = 3031;

UPDATE `item_template` SET
    `name` = 'Fel Iron Arrow', `Quality` = 2, `Flags` = 0, `FlagsExtra` = 0,
    `BuyCount` = 200, `BuyPrice` = 8000, `SellPrice` = 20, `ItemLevel` = 97, `RequiredLevel` = 57,
    `stackable` = 1000, `dmg_min1` = 26, `dmg_max1` = 26, `delay` = 3000, `BagFamily` = 1
WHERE `entry` = 3029;

-- The old "Deprecated ..." translations would otherwise name them on non-English clients.
DELETE FROM `item_template_locale` WHERE `ID` IN (3029, 3031);
