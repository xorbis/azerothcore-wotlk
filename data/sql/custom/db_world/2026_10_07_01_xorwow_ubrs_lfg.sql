-- XorWoW: Upper Blackrock Spire (LFGDungeons 44) in the dungeon finder as a level 58-65 Classic
-- dungeon, under Specific Dungeons only. The LFGDungeons row itself is changed by the client patch
-- (client-patch\patch.json, server half in 2026_10_02_00_xorwow_client_patch_dbc.sql); LFGMgr keeps
-- it out of the Random Classic Dungeon pool. Same map (229) and entrance as Lower Blackrock Spire.
-- General Drakkisath already completes it (instance_encounters 280). Idempotent.
DELETE FROM `lfg_dungeon_template` WHERE `dungeonId` = 44;
INSERT INTO `lfg_dungeon_template` (`dungeonId`, `name`, `position_x`, `position_y`, `position_z`, `orientation`, `VerifiedBuild`)
SELECT 44, 'Upper Blackrock Spire', `position_x`, `position_y`, `position_z`, `orientation`, 0
FROM `lfg_dungeon_template` WHERE `dungeonId` = 32;
