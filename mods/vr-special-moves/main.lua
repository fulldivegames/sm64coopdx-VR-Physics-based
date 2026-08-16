-- name: VR Special Moves Runtime
-- description: Multiplayer manifest for SM64 Co-Op DX VR Special Moves. The matching native VR build supplies tracked-hand physics and rendering.
-- category: misc
-- pausable: true

local SPECIAL_MOVES_PROTOCOL = 1

if network_is_server() then
    gGlobalSyncTable.sm64VrSpecialMovesProtocol = SPECIAL_MOVES_PROTOCOL
end
