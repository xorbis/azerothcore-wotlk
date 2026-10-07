-- XorWoW: Golden Offering (spell 260011), the guild hall's gold coin toss: 1 gold per cast into the
-- caster's guild bank (src/server/scripts/Custom/xorwow_guild_hall_offering.cpp). The spell row and
-- its AreaGroup (2629, the guild hall areas) are in 2026_10_02_00_xorwow_client_patch_dbc.sql.
-- Idempotent.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_xorwow_golden_offering';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (260011, 'spell_xorwow_golden_offering');
