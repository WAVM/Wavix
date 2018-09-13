cmake_minimum_required(VERSION 3.10)
add_executable(helloworld HelloWorld.cpp)
install(TARGETS helloworld
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib/static)
