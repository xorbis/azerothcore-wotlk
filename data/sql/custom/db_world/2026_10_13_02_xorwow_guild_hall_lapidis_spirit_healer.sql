-- XorWoW: a Spirit Healer in the Island of Dr Lapidis guild hall (maps 731/732), where the user
-- stood (2026-10-13); any spirit healer already within 40 yards of it goes first. A static spawn:
-- every guild's instance of the hall has it. Idempotent.
SET @HEALER := 6491;

DELETE FROM `creature` WHERE `id` IN (6491, 29259, 32537) AND `map` IN (731, 732)
  AND SQRT(POW(`position_x` - -11908.1, 2) + POW(`position_y` - 3301.32, 2)) < 40;

SET @CGUID := (SELECT MAX(`guid`) FROM `creature`);
INSERT INTO `creature`
(`guid`,`id`,`map`,`zoneId`,`areaId`,`spawnMask`,`phaseMask`,`equipment_id`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecs`,`wander_distance`,`currentwaypoint`,`curhealth`,`curmana`,`MovementType`,`npcflag`,`unit_flags`,`dynamicflags`,`ScriptName`,`VerifiedBuild`,`CreateObject`,`Comment`) VALUES
(@CGUID+1,@HEALER,731,4994,4994,1,1,0,-11908.1,3301.32,23.8036,5.57035,300,0,0,1,0,0,0,0,0,'',0,0,'Spirit Healer - XorWoW Alliance Guild Hall (Island of Dr Lapidis)'),
(@CGUID+2,@HEALER,732,4995,4995,1,1,0,-11908.1,3301.32,23.8036,5.57035,300,0,0,1,0,0,0,0,0,'',0,0,'Spirit Healer - XorWoW Horde Guild Hall (Island of Dr Lapidis)');
