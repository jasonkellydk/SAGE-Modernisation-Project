# Runtime DLL deployment for executable tests. An empty dependency set is a
# valid static build; keep the command usable for either dependency topology.
foreach(runtime_file IN LISTS RUNTIME_FILES)
	get_filename_component(runtime_name "${runtime_file}" NAME)
	file(COPY_FILE "${runtime_file}" "${OUTPUT_DIRECTORY}/${runtime_name}" ONLY_IF_DIFFERENT)
endforeach()
