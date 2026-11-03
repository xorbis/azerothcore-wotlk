-- XorWoW: Ritual of Summoning needs 2 players, the warlock included, bot or human (user, 2026-11-02).
-- data0 = reqParticipants, counts the caster. Absolute value, idempotent.
UPDATE `gameobject_template` SET `Data0` = 2 WHERE `entry` IN (
    194108, -- Summoning Portal, Ritual of Summoning 698 (3)
    36727   -- Summoning Portal, old Ritual of Summoning portal (3)
);
