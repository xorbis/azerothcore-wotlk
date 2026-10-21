-- XorWoW: Alterac Valley mine workers never mined (user, 2026-10-18). BattlegroundAV spawns the mine population
-- as temporary creatures at hardcoded positions, so the only place an idle animation can come from is
-- creature_template_addon, and none of the workers had a row: Coldmine Peons stood still with their picks.
-- EMOTE_STATE_WORK_MINING (233) is what the world's other miners use; it is reapplied on evade.
-- Guards, explorers, troggs and the bosses keep standing. Idempotent.
INSERT INTO `creature_template_addon` (`entry`, `emote`) VALUES
(11603, 233), -- Whitewhisker Digger (Coldtooth Mine, neutral)
(13316, 233), -- Coldmine Peon (Coldtooth Mine, Horde)
(13317, 233), -- Coldmine Miner (Coldtooth Mine, Alliance)
(13396, 233), -- Irondeep Miner (Irondeep Mine, Alliance)
(13397, 233)  -- Irondeep Peon (Irondeep Mine, Horde)
ON DUPLICATE KEY UPDATE `emote` = VALUES(`emote`);
