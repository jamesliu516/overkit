# Copyright (c) 2020 Matthew J. Smith and Overkit contributors
# License: MIT (http://opensource.org/licenses/MIT)

file(GLOB GLOBBED_FILES ${FILES})
file(COPY ${GLOBBED_FILES} DESTINATION ${DEST_DIR})
