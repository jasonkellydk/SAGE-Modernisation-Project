-- Toolkit.cpp:1813, EA 3e00c3a1b97381bb28be89a35b856375e0629a08.
-- The cinematic supplies ConvName; preparation resolves its retail dependency.
return {
    ConversationParameters={"ConvName"},
    Created=function(self,object,conversation)
        emit("Start_Conversation",object,conversation,99,2000,100000)
    end
}
