-- XorWoW: AQ40 is a 25-player raid here (raid sizes, 2026-10-18), C'Thun was still tuned for 40
-- (user, 2026-10-27): same scale as the Twin Emperors, 60% health and 70% damage for the boss and every tentacle.
-- Absolute values, idempotent.
UPDATE `creature_template` SET `HealthModifier` = 240, `DamageModifier` = 10.395 WHERE `entry` = 15589; -- Eye of C'Thun (400, 14.85)
UPDATE `creature_template` SET `HealthModifier` = 180, `DamageModifier` = 0.735 WHERE `entry` = 15727;  -- C'Thun (300, 1.05)
UPDATE `creature_template` SET `HealthModifier` = 0.45, `DamageModifier` = 1.4 WHERE `entry` = 15726;   -- Eye Tentacle (0.75, 2)
UPDATE `creature_template` SET `HealthModifier` = 0.3, `DamageModifier` = 1.4 WHERE `entry` = 15725;    -- Claw Tentacle (0.5, 2)
UPDATE `creature_template` SET `HealthModifier` = 18, `DamageModifier` = 27.685 WHERE `entry` = 15728;  -- Giant Claw Tentacle (30, 39.55)
UPDATE `creature_template` SET `HealthModifier` = 7.2, `DamageModifier` = 6.965 WHERE `entry` = 15334;  -- Giant Eye Tentacle (12, 9.95)
UPDATE `creature_template` SET `HealthModifier` = 4.8, `DamageModifier` = 2.625 WHERE `entry` = 15802;  -- Flesh Tentacle (8, 3.75)
