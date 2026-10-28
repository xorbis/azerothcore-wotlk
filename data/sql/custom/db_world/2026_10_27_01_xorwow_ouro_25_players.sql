-- XorWoW: AQ40 is a 25-player raid here (raid sizes, 2026-10-18), Ouro was still tuned for 40
-- (user, 2026-10-27): same scale as the Twin Emperors and C'Thun, 60% health and 70% damage for the boss and his scarabs.
-- Absolute values, idempotent.
UPDATE `creature_template` SET `HealthModifier` = 360, `DamageModifier` = 33.11 WHERE `entry` = 15517; -- Ouro (600, 47.3)
UPDATE `creature_template` SET `HealthModifier` = 1.8, `DamageModifier` = 1.575 WHERE `entry` = 15718; -- Ouro Scarab (3, 2.25)
