-- XorWoW: AQ40 Twin Emperors can be taunted (user, 2026-10-26). Effects 114 (ATTACK_ME) and Auras 11
-- (MOD_TAUNT) were their taunt immunity; their CC immunities are kept. Both rows are used by these two
-- bosses only (Vek'lor -351, Vek'nilash -282). Idempotent.
UPDATE `creature_immunities` SET `Effects` = '', `Auras` = '', `Comment` = REPLACE(`Comment`, ', flags=NO_TAUNT, effects=114(ATTACK_ME), auras=11(BIND)', '') WHERE `ID` IN (-351, -282);
