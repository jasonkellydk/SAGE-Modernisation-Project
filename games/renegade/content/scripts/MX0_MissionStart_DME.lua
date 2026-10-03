-- MissionX0.cpp MX0_MissionStart_DME::Created, EA revision
-- 3e00c3a1b97381bb28be89a35b856375e0629a08. State and effects belong to ECS.
-- Integer durations are milliseconds; the mission host supplies star identity.
return {
    Startups={"Cinematic"},
    Created = function(self, object, star)
        emit("Select_Weapon", star, "")
        emit("Initialize_State", object, 0, 0, 0, 0, 0, 0, 0, 0)
        emit("Fade_Background_Music", "renegade_intro_no_vox.mp3", 0, 1000)
        emit("Start_Cinematic", object, "Invisible_Object", "X00_Intro.txt", 0, 0, 0)
    end
}
