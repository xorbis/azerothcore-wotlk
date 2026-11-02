-- XorWoW: halve the Anubisath Defender's health in AQ40 (user, 2026-11-01).
-- Absolute value, idempotent.
UPDATE `creature_template` SET `HealthModifier` = 75 WHERE `entry` = 15277; -- Anubisath Defender (150)
