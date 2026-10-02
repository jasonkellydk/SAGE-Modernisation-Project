-- Code/Scripts/Mission03.cpp: M03_Destroyed_Turret.
-- Timing crosses the host API as integer milliseconds; the host converts to
-- fixed simulation ticks. Mission logic and sound names belong to this game.
return {
    Created = function(self, object)
        emit("Start_Timer", object, 1000, 0)
        emit("Start_Timer", object, 4000, 1)
    end,
    Timer_Expired = function(self, object, timer_id)
        if timer_id == 0 then
            emit("Create_2D_Sound", "EVA_Enemy_Structure_Destroyed")
        end
    end
}
