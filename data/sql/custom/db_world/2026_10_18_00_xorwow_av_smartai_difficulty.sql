-- XorWoW: Alterac Valley bunker/tower bowmen never shot (user, 2026-10-18), and other AV NPCs never used their abilities.
-- Their SmartAI events carry the "Normal Dungeon" difficulty flag, but SmartScript only loads difficulty-flagged
-- events on dungeon maps (IsDungeon()), and AV is a battleground map, so the events were silently dropped.
-- The bowmen were left with plain melee AI while rooted by BattlegroundAV. Clearing the difficulty bits
-- (0x02-0x10) loads the events on any map; nothing else about the events changes. Idempotent.
UPDATE `smart_scripts` SET `event_flags` = `event_flags` & ~0x1E
WHERE `source_type` = 0 AND (`event_flags` & 0x1E) <> 0 AND `entryorguid` IN (
    13358, 13359,                                     -- Stormpike / Frostwolf Bowman (bunkers and towers)
    13331, 13332,                                     -- Veteran Defender / Veteran Guardian (graveyards)
    13236,                                            -- Primalist Thurloga
    11600, 11602, 11604, 11657, 13079, 13080, 13081, 13098, 13099, 13396, 13397, -- Irondeep Mine
    11675, 11677, 13078, 13086, 13087, 13096, 13317); -- Coldtooth Mine and the rest
