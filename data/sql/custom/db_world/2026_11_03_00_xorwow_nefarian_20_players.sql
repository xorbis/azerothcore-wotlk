-- XorWoW: BWL is a 20-player raid here (raid sizes, 2026-10-16), Nefarian was still tuned for 40
-- (user, 2026-11-03): half the raid, so 50% health and 60% damage for the boss and every add of the encounter
-- (AQ40's 25/40 got 60% and 70%, same formula). Absolute values, idempotent.
-- The 42 drakonids of phase 1 stay: half the health for half the raid keeps the phase length.
UPDATE `creature_template` SET `HealthModifier` = 325, `DamageModifier` = 21 WHERE `entry` = 11583;    -- Nefarian (650, 35)
UPDATE `creature_template` SET `HealthModifier` = 57.5, `DamageModifier` = 2.1 WHERE `entry` = 10162;  -- Lord Victor Nefarius (115, 3.5)
UPDATE `creature_template` SET `HealthModifier` = 2.5, `DamageModifier` = 3 WHERE `entry` IN (14261, 14262, 14263, 14264, 14265); -- Blue/Green/Bronze/Red/Black Drakonid (5, 5)
UPDATE `creature_template` SET `HealthModifier` = 4, `DamageModifier` = 3.6 WHERE `entry` = 14302;     -- Chromatic Drakonid (8, 6)
UPDATE `creature_template` SET `HealthModifier` = 1, `DamageModifier` = 4.5 WHERE `entry` = 14605;     -- Bone Construct (2, 7.5)
UPDATE `creature_template` SET `HealthModifier` = 0.8, `DamageModifier` = 1.41 WHERE `entry` = 14668;  -- Corrupted Infernal (1.6, 2.35)
