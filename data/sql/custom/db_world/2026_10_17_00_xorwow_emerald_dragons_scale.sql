-- XorWoW: the four Emerald Dragons (Ysondre, Lethon, Emeriss, Taerar) at 60% size (user, 2026-10-17).
-- At full size they could not follow players down their altar stairs; DisplayScale scales the model,
-- bounding radius and combat reach (18 -> 10.8) together. Their wider leash is in boss_emerald_dragons.cpp.
-- Idempotent.
UPDATE `creature_template_model` SET `DisplayScale` = 0.6 WHERE `CreatureID` IN (14887, 14888, 14889, 14890);
