# Multiplayer-first networking: every game (single player included) runs
# through a lockstep server; transports are adapters (loopback in-process).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_net
	MODULES
		transport/connection.cppm
		transport/udp_socket.cppm
		transport/reliable_udp.cppm
		lockstep/protocol.cppm
		lockstep/command_log.cppm
		lockstep/hash_vote.cppm
		lockstep/input_delay.cppm
		lockstep/lockstep_server.cppm
		lockstep/lockstep_client.cppm
		lockstep/lockstep_peer.cppm
		lockstep/local_match.cppm
	DEPENDS engine_core_serialization
	TESTS net.test transport/udp_socket.test transport/reliable_udp.test
	TESTS_OPTION ENGINE_NET_BUILD_TESTS)
# The platform sockets stay in the implementation unit.
target_sources(engine_net PRIVATE transport/udp_socket.cpp)
set_source_files_properties(transport/udp_socket.cpp PROPERTIES LANGUAGE CXX)
if(WIN32)
	target_link_libraries(engine_net PUBLIC ws2_32 iphlpapi)
endif()
