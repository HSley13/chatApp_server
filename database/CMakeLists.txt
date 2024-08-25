pkg_check_modules(MONGOCXX REQUIRED libmongocxx)
pkg_check_modules(BSONCXX REQUIRED libbsoncxx)

add_library(database_library STATIC database.cpp)

target_link_libraries(database_library PUBLIC
                                        Qt6::Widgets
                                        Qt6::WebSockets
                                        ${MONGOCXX_LIBRARIES} 
                                        ${BSONCXX_LIBRARIES}
                                        ${AWSSDK_LINK_LIBRARIES} 

                    )

target_link_directories(database_library PUBLIC
                                        ${MONGOCXX_LIBRARY_DIRS} 
                                        ${BSONCXX_LIBRARY_DIRS}
                                        ${BSONCXX_LIBRARY_DIRS}
                        )


target_include_directories(database_library PUBLIC 
                                            ${MONGOCXX_INCLUDE_DIRS} 
                                            ${BSONCXX_INCLUDE_DIRS}
                                            ${AWSSDK_INCLUDE_DIR}
                            )

target_include_directories(database_library INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})