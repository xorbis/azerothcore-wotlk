-- XorWoW: stable slots cost half the stock price. The prices come from StableSlotPrices.dbc
-- (slot 1: 5 silver, 2: 5 gold, 3: 50 gold, 4: 150 gold); rows in stableslotprices_dbc replace
-- the DBC's at worldserver startup (no reload command, restart). Cost is in copper. The stock
-- client reads its own copy of the DBC for the price it shows, so the XorWoW addon shows these
-- instead and buys through ".stable buyslot" (src/server/scripts/Custom/xorwow_stable_slot.cpp).
-- Idempotent.
DELETE FROM `stableslotprices_dbc` WHERE `ID` BETWEEN 1 AND 4;
INSERT INTO `stableslotprices_dbc` (`ID`, `Cost`) VALUES
(1,    250),
(2,  25000),
(3, 250000),
(4, 750000);
