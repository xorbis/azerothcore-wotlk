-- XorWoW: the Warpweavers of mod-transmog-plus (creature 190012, "Transmogrifier") stand next to the
-- Huntmasters of mod-hunts in every capital, 5 yd to the side and facing the same way, so the guards'
-- "Huntmaster & Transmogrifier" direction (Hunts.GuardDirections.*) points at both. The spots were
-- taken from the server's own navmesh (mmaps): walkable, same floor as the Huntmaster, nothing within
-- 3 yd. Ironforge is the exception - its Huntmaster stands on the 3-yd-wide landing of the Timberline
-- Arms stairs with drops on both sides, so the Warpweaver is 3 yd further up the same landing.
-- Re-applying this file replaces the spawns (DELETE by creature id).
SET @WARPWEAVER := 190012;

DELETE FROM `creature` WHERE `id` = @WARPWEAVER;
SET @CGUID := (SELECT MAX(`guid`) FROM `creature`);

INSERT INTO `creature`
(`guid`,`id`,`map`,`zoneId`,`areaId`,`spawnMask`,`phaseMask`,`equipment_id`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecs`,`wander_distance`,`currentwaypoint`,`curhealth`,`curmana`,`MovementType`,`npcflag`,`unit_flags`,`dynamicflags`,`ScriptName`,`VerifiedBuild`,`CreateObject`,`Comment`) VALUES
(@CGUID+1,@WARPWEAVER,0,0,0,1,1,0,-8771.175,642.369,97.232,3.0328,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Stormwind'),
(@CGUID+2,@WARPWEAVER,0,0,0,1,1,0,-5034.816,-1192.312,507.435,5.2310,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Ironforge'),
(@CGUID+3,@WARPWEAVER,1,0,0,1,1,0,9947.253,2277.709,1341.450,0.0175,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Darnassus'),
(@CGUID+4,@WARPWEAVER,530,0,0,1,1,0,-4187.826,-11555.348,-125.678,3.6521,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Exodar'),
(@CGUID+5,@WARPWEAVER,1,0,0,1,1,0,1855.765,-4511.680,23.893,3.5013,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Orgrimmar'),
(@CGUID+6,@WARPWEAVER,1,0,0,1,1,0,-1406.120,-147.520,159.282,1.7206,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Thunder Bluff'),
(@CGUID+7,@WARPWEAVER,0,0,0,1,1,0,1471.401,36.558,-62.260,1.5992,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Undercity'),
(@CGUID+8,@WARPWEAVER,530,0,0,1,1,0,9796.807,-7326.507,14.842,2.0061,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Silvermoon'),
(@CGUID+9,@WARPWEAVER,530,0,0,1,1,0,-2017.582,5207.816,-35.651,5.9164,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Shattrath'),
(@CGUID+10,@WARPWEAVER,571,0,0,1,1,0,5770.066,552.273,651.529,0.8436,300,0,0,1,0,0,0,0,0,'',0,0,'Warpweaver - Dalaran');
