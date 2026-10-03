-- Mission00.cpp MTU_Trigger_Zone: obstacle-course entry branches. The source
-- crouch trigger is commented out; later zones remain explicit unported work.
return {
    Created=function(self,object)
        self[1],self[2]=1,0 -- trigger_once, gave_elevator_help
    end,
    Entered=function(self,object,enterer)
        if object==300001 then
            emit("Set_HUD_Help_Text","IDS_MTUDSGN_DSGN0384I1DSGN_TXT","0.196","0.882","0.196")
            emit("Destroy_Object",object)
        elseif object>=400015 and object<=400018 then
            emit("Send_Custom_Event",object,400005,2,object-400010,0)
            emit("Destroy_Object",object)
        elseif object~=400014 then
            emit("Unported_Callback","Entered",object,enterer)
        end
    end,
    Custom=function(self,object,event,parameter,sender)
        emit("Unported_Callback","Custom",event,parameter)
    end
}
