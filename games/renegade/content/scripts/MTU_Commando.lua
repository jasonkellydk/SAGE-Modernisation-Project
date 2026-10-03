-- Mission00.cpp MTU_Commando. Slot 1 preserves sydney_shot. Camera timers
-- and damage responses remain explicit unfinished callbacks.
return {
    Created=function(self,object)
        emit("Reveal_Encyclopedia_Weapon",14)
        emit("Select_Weapon",object,"")
        self[1]=0
    end,
    Custom=function(self,object,event,parameter,sender)
        if event==1 then
            if parameter==2 then
                emit("Control_Enable",object,0)
            elseif parameter==1 then
                emit("Control_Enable",object,1)
                emit("Action_Follow_Input",object,100,0)
            elseif parameter==14 then
                self[1]=0
            end
        else
            emit("Unported_Callback","Custom",event,parameter)
        end
    end,
    Timer_Expired=function(self,object,timer)
        emit("Unported_Callback","Timer_Expired",timer)
    end,
    Damaged=function(self,object,damager,amount)
        emit("Unported_Callback","Damaged",damager,amount)
    end
}
