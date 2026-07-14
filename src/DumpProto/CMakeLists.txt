add_executable(DumpProto
	CsvFile.cpp
	CsvFile.h
	ItemCSVReader.cpp
	ItemCSVReader.h
	dump_proto.cpp
	lzo.cpp
	lzo.h
	Singleton.h
	# Use tea.cpp from EterBase instead of maintaining a local copy.
	"${CMAKE_CURRENT_SOURCE_DIR}/../EterBase/tea.cpp"
)
set_target_properties(DumpProto PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_include_directories(DumpProto PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../EterBase
)

target_link_libraries(DumpProto
	lzo2
	sodium
)
