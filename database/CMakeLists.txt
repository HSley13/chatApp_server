add_library(database_library STATIC database.cpp)

target_link_libraries(database_library PUBLIC
                                        Qt6::Core 
                                        Qt6::Widgets
                                        Qt6::Multimedia
                                        Qt6::WebSockets
                                        argon2
                                        ${MONGOCXX_LIBRARIES} 
                                        ${BSONCXX_LIBRARIES}
                                        ${AWSSDK_LINK_LIBRARIES} 

                    )

pkg_check_modules(MONGOCXX REQUIRED libmongocxx)
pkg_check_modules(BSONCXX REQUIRED libbsoncxx)

target_link_directories(database_library PUBLIC
                                        /opt/homebrew/lib 
                                        ${MONGOCXX_LIBRARY_DIRS} 
                                        ${BSONCXX_LIBRARY_DIRS}
                                        ${BSONCXX_LIBRARY_DIRS}
                        )


target_include_directories(database_library PUBLIC 
                                            /opt/homebrew/include
                                            ${MONGOCXX_INCLUDE_DIRS} 
                                            ${BSONCXX_INCLUDE_DIRS}
                                            ${AWSSDK_INCLUDE_DIR}
                            )

target_include_directories(database_library INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})