-- XorWoW: a Spirit Healer in the Gillijim's Isle guild hall (maps 733/734), 6 yards east of where a
-- ghost released inside appears (its graveyards 1729/1730, the hall's entrance), facing it (user,
-- 2026-10-13); any spirit healer already within 40 yards of it goes first. A static spawn: every
-- guild's instance of the hall has it. Idempotent.
SET @HEALER := 6491;

DELETE FROM `creature` WHERE `id` IN (6491, 29259, 32537) AND `map` IN (733, 734)
  AND SQRT(POW(`position_x` - -13925.0, 2) + POW(`position_y` - 3206.5, 2)) < 40;

SET @CGUID := (SELECT MAX(`guid`) FROM `creature`);
INSERT INTO `creature`
(`guid`,`id`,`map`,`zoneId`,`areaId`,`spawnMask`,`phaseMask`,`equipment_id`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecs`,`wander_distance`,`currentwaypoint`,`curhealth`,`curmana`,`MovementType`,`npcflag`,`unit_flags`,`dynamicflags`,`ScriptName`,`VerifiedBuild`,`CreateObject`,`Comment`) VALUES
(@CGUID+1,@HEALER,733,4996,4996,1,1,0,-13925.0,3206.5,19.0,1.5708,300,0,0,1,0,0,0,0,0,'',0,0,'Spirit Healer - XorWoW Alliance Guild Hall (Gillijim''s Isle)'),
(@CGUID+2,@HEALER,734,4997,4997,1,1,0,-13925.0,3206.5,19.0,1.5708,300,0,0,1,0,0,0,0,0,'',0,0,'Spirit Healer - XorWoW Horde Guild Hall (Gillijim''s Isle)');
