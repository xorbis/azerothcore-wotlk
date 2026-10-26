-- XorWoW: AQ40 is a 25-player raid here (raid sizes, 2026-10-18), the Twin Emperors were still tuned for 40
-- (user, 2026-10-26): 60% health (650 -> 390) and 70% damage (30 -> 21). Absolute values, idempotent.
UPDATE `creature_template` SET `HealthModifier` = 390, `DamageModifier` = 21 WHERE `entry` = 15275;
UPDATE `creature_template` SET `HealthModifier` = 390, `DamageModifier` = 21.04 WHERE `entry` = 15276;
