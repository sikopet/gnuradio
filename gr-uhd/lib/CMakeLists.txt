# Copyright 2011 Free Software Foundation, Inc.
# 
# This file is part of GNU Radio
# 
# GNU Radio is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
# 
# GNU Radio is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

########################################################################
# Setup the include and linker paths
########################################################################
include_directories(
    ${GNURADIO_CORE_INCLUDE_DIRS}
    ${GR_UHD_INCLUDE_DIRS}
)

include_directories(${UHD_INCLUDE_DIRS})
link_directories(${UHD_LIBRARY_DIRS})

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

########################################################################
# Setup library
########################################################################
list(APPEND gr_uhd_sources
    gr_uhd_usrp_source.cc
    gr_uhd_usrp_sink.cc
    gr_uhd_amsg_source.cc
)

list(APPEND uhd_libs
    gnuradio-core
    ${Boost_LIBRARIES}
    ${UHD_LIBRARIES}
)

add_library(gnuradio-uhd SHARED ${gr_uhd_sources})
target_link_libraries(gnuradio-uhd ${uhd_libs})
set_target_properties(gnuradio-uhd PROPERTIES DEFINE_SYMBOL "gnuradio_uhd_EXPORTS")
set_target_properties(gnuradio-uhd PROPERTIES SOVERSION ${LIBVER})

install(TARGETS gnuradio-uhd
    LIBRARY DESTINATION ${GR_LIBRARY_DIR} COMPONENT "uhd_runtime" # .so/.dylib file
    ARCHIVE DESTINATION ${GR_LIBRARY_DIR} COMPONENT "uhd_devel" # .lib file
    RUNTIME DESTINATION ${GR_RUNTIME_DIR} COMPONENT "uhd_runtime" # .dll file
)