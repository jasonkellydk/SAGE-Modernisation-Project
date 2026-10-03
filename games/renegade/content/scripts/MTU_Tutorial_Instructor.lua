-- Mission00.cpp: instructor startup and Logan's introductory speech request.
-- All state uses the original SAVE_VARIABLE slot numbers in ECS columns.
local function say(object,name,action,look)
    emit("Stop_All_Conversations")
    emit("Face_Actor",object,1)
    if look then emit("Camera_Look_Actor",object,"1.5") end
    emit("Start_Conversation",object,name,100,300,action)
end
local function control(object,enabled)
    emit("Send_Custom_Event",object,1,1,enabled and 1 or 2,0)
end
local function move(object,path,speed,action)
    emit("Action_Goto_Waypath",object,path,speed,"1",100,action)
end
local function help(descriptor)
    emit("Set_HUD_Help_Text",descriptor,"0.196","0.882","0.196")
end
return {
    Animations={"CHT_JAIL.CHT_JAIL"},
    Conversations={"MTU_LOGAN_START","MTU_LOGAN_JUMP_TEST","MTU_LOGAN_EVA","MTU_LOGAN_POKE","MTU_LOGAN_COURSE_DONE","MTU_LOGAN_KEYCARDS","MTU_LOGAN_GO_INSIDE"},
    Created=function(self,object)
        for slot=1,19 do self[slot]=0 end
        emit("Set_Loiters_Allowed",object,0)
        emit("Innate_Disable",object)
        emit("Set_Shield_Type",object,"Blamo")
        emit("Enable_Hibernation",object,0)
    end,
    Custom=function(self,object,event,parameter,sender)
        if event==2 and parameter==3 then
            say(object,"MTU_LOGAN_START",5,true)
        elseif event==2 and parameter==5 then
            move(object,400049,"1",1)
            control(object,false)
        elseif event==2 and parameter==6 then
            control(object,false)
            say(object,"MTU_LOGAN_EVA",12,true)
        elseif event==2 and parameter==7 then
            control(object,false)
            move(object,400074,"1",3)
        elseif event==2 and parameter==8 then
            control(object,false)
            -- Original keycard camera point includes the source 1.5m offset.
            emit("Stop_All_Conversations")
            emit("Face_Actor",object,1)
            emit("Force_Camera_Look","-12.776","23.761","1")
            emit("Start_Conversation",object,"MTU_LOGAN_KEYCARDS",100,300,16)
        else
            emit("Unported_Callback","Custom",event,parameter)
        end
    end,
    Action_Complete=function(self,object,action,reason)
        if reason==6 or reason==7 or reason==8 then
            if action==5 then
                help("IDS_M01DSGN_DSGN0515I1DSGN_TXT")
                emit("Set_Animation",400142,"CHT_JAIL.CHT_JAIL",0,0,2)
                emit("Set_Animation",400143,"CHT_JAIL.CHT_JAIL",0,0,2)
            elseif action==11 then
                help("IDS_MTUDSGN_DSGN0383I1DSGN_TXT")
                control(object,true)
                move(object,400067,"0.3",2)
                emit("Set_Animation",400144,"CHT_JAIL.CHT_JAIL",0,0,2)
            elseif action==12 then
                help("IDS_M00DSGN_DSGN1004I1DSGN_TXT")
                emit("Set_Objective_Status",1,0)
                emit("Set_Objective_Radar_Blip",1,"-12.39","20.78","1.81")
                emit("Set_Objective_HUD_Info_Position",1,99,"POG_M00_1_01.tga","IDS_POG_LOCATE","-12.39","20.78","1.81")
                say(object,"MTU_LOGAN_POKE",13,false)
            elseif action==13 then
                help("IDS_MTUDSGN_DSGN0385I1DSGN_TXT")
                emit("Select_Weapon",1,"Weapon_Pistol_Player")
                control(object,true)
            elseif action==15 then
                help("IDS_M00DSGN_DSGN1003I1DSGN_TXT")
                control(object,true)
                emit("Action_Goto_Position",object,"-13.094","32.111","1.023","1","1",100,4)
            elseif action==16 then
                help("IDS_MTUDSGN_DSGN0386I1DSGN_TXT")
                emit("Create_Object","Level_01_Keycard","-12.637","24.426","0")
                say(object,"MTU_LOGAN_GO_INSIDE",17,false)
                control(object,true)
            else
                emit("Unported_Callback","Action_Complete",action,reason)
            end
        elseif action==1 then
            emit("Set_Position",object,"-53.697","-5.334","2")
            say(object,"MTU_LOGAN_JUMP_TEST",11,true)
        elseif action==3 then
            emit("Set_Position",object,"-35.01","-15.658","0.705")
            say(object,"MTU_LOGAN_COURSE_DONE",15,true)
        else
            emit("Unported_Callback","Action_Complete",action,reason)
        end
    end
}
