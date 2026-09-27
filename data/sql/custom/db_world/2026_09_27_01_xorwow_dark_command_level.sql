-- XorWoW: Dark Command (56222, the Death Knight taunt) is trained at level 60 instead of 65, so
-- DKs have their taunt under the level 60 cap (see 2026_09_27_00_xorwow_expansion_lock.sql).
-- Bots learn from the same trainer data. Undo: set ReqLevel back to 65. Idempotent.
UPDATE `trainer_spell` SET `ReqLevel` = 60 WHERE `SpellId` = 56222;
