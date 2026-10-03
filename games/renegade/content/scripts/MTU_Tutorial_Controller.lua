-- Mission00.cpp: MTU_Tutorial_Controller::Created and mission-start timer.
-- Slots use the original SAVE_VARIABLE numbers. Remaining controller events
-- are reported explicitly while their gameplay dependencies are ported.
local mission_start, flyovers = 1, 22
local logan, tower = 400005, 450939
return {
    Animations={"CHT_JAIL.CHT_JAIL"},
    ObjectiveTextures={"POG_M00_1_01.tga","POG_M00_1_02.tga","POG_M00_1_03.tga",
        "POG_M00_1_04.tga","POG_M00_1_05.tga","POG_M00_1_06.tga","HUD_STAR.TGA","HUD_obje_arrow.TGA"},
    Created = function(self, object, parameters, first_flyover_seconds)
        for slot=1,22 do self[slot]=0 end
        self[8],self[18],self[19],self[10]=1,1,1,-1
        emit("Reveal_Map")
        local markers={
            {12,"29.63","11.98","-8.04"}, {13,"10.15","-19.68","-8.36"},
            {14,"-43.23","25.66","-6.32"}, {15,"-25.33","56.15","-7.56"},
            {16,"-7.89","15.57","-8.75"}
        }
        for index=1,5 do
            local marker=markers[index]
            emit("Add_Radar_Marker",marker[1],marker[2],marker[3],marker[4],3,1)
        end
        for objective=1,6 do
            -- Combat/string_ids.h: the retail short/long translations alternate.
            emit("Add_Objective",objective,1,3,7503+objective*2,
                "IDS_M00EVAG_DSGN0056I1EVAG_SND",7504+objective*2)
        end
        emit("Start_Timer",object,2000,mission_start)
        emit("Start_Timer",object,first_flyover_seconds*1000,flyovers)
        local gates={400142,400143,400144,400146}
        for index=1,4 do emit("Set_Animation_Frame",gates[index],"CHT_JAIL.CHT_JAIL",0) end
    end,
    Timer_Expired = function(self,object,timer)
        if timer==mission_start then
            emit("Send_Custom_Event",object,logan,2,3,0)
            emit("Send_Custom_Event",object,tower,0,0,0)
        else
            emit("Unported_Callback","Timer_Expired",timer)
        end
    end
}
