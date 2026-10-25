-- XorWoW: AQ40 Twin Emperors lose their school immunities (user, 2026-10-25), unviable with bots that
-- cannot pick their target by school. Vek'lor (-351) was immune to physical, Vek'nilash (-282) to all magic.
-- Both rows are used by these two bosses only; their CC, taunt and bind immunities are kept.
-- Idempotent.
UPDATE `creature_immunities` SET `SchoolMask` = 0, `Comment` = REPLACE(`Comment`, 'school=0x1(NORMAL), ', '') WHERE `ID` = -351;
UPDATE `creature_immunities` SET `SchoolMask` = 0, `Comment` = REPLACE(`Comment`, 'school=0x7C(FIRE|NATURE|FROST|SHADOW|ARCANE), ', '') WHERE `ID` = -282;
