-- Mission00.cpp MTU_Commando_Startup::Created; god.cpp SINGLE_INIT reads
-- this identity from the saved level's CombatManager state.
return {
    Scripts={"MTU_Commando"},
    Created=function(self,object)
        emit("Attach_Script",object,"MTU_Commando","")
    end
}
