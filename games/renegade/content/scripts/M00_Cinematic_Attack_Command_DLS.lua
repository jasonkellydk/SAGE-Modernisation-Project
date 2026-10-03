-- Test_Cinematic.cpp: Created force-fires ten units ahead and two units up;
-- the delayed self custom event resets actions at priority 100. Positions,
-- direction and duration use the host's Q16 integers so Lua stays deterministic.
return {
    Created = function(self, owner, x, y, z, forward_x, forward_y, duration)
        emit("Action_Attack", owner, 99, 1, x + forward_x * 10,
             y + forward_y * 10, z + 2 * 65536, 100 * 65536, 0, 1, 1)
        emit("Send_Custom_Event", owner, owner, 1, 1, duration)
    end,
    Custom = function(self, owner, event_type, parameter, sender)
        emit("Action_Reset", owner, 100)
    end
}
