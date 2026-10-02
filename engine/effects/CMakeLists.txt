# Effects: presentation-side visual effects (particle systems, ...) the game
# client runs from what the simulation did. Never read by the simulation.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_effects_particles
	MODULES
		particles/definitions/particle_system_definition.cppm
		particles/simulation/particle_world.cppm
	DEPENDS engine_jobs
	TESTS particles/particles.test
	TESTS_OPTION ENGINE_EFFECTS_BUILD_TESTS)
if(TARGET engine_effects_particles_tests)
	set_tests_properties(engine_effects_particles_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()
