-- XorWoW: Unholy Presence (48265) is trained at level 60 instead of 70, so Death Knights have all
-- three presences under the level 60 cap (see 2026_09_27_00_xorwow_expansion_lock.sql). The price
-- is unchanged. Bots learn from the same trainer data. Undo: set ReqLevel back to 70. Idempotent.
UPDATE `trainer_spell` SET `ReqLevel` = 60 WHERE `SpellId` = 48265;
