ACLOCAL_AMFLAGS=-I m4
SUBDIRS = inc

bin_SCRIPTS = scripts/bin/*

pyutilsdir=$(libdir)/opx
pyutils_SCRIPTS = scripts/lib/python/*.py

lib_LTLIBRARIES=libopx_nas_qos.la

libopx_nas_qos_la_SOURCES=src/nas_qos_cps_map_entry.cpp src/nas_qos_policer.cpp src/nas_qos_buffer_profile.cpp src/nas_qos_map_util.cpp src/nas_qos_switch.cpp src/nas_qos_cps_scheduler.cpp src/nas_qos_map.cpp src/nas_qos_port_ingress.cpp src/nas_qos_cps_buffer_profile.cpp src/nas_qos_scheduler_group.cpp src/nas_qos_switch_list.cpp src/nas_qos_cps_buffer_pool.cpp src/nas_qos_map_entry.cpp src/nas_qos_cps_port_egress.cpp src/nas_qos_cps_wred.cpp src/nas_qos_cps_queue.cpp src/nas_qos_queue.cpp src/nas_qos_init.cpp src/nas_qos_cps_port_ingress.cpp src/nas_qos_wred.cpp src/nas_qos_cps_policer.cpp src/nas_qos_buffer_pool.cpp src/nas_qos_cps_map.cpp src/nas_qos_cps_priority_group.cpp src/nas_qos_port_egress.cpp src/nas_qos_cps.cpp src/nas_qos_cps_scheduler_group.cpp src/nas_qos_scheduler.cpp src/nas_qos_priority_group.cpp

libopx_nas_qos_la_CPPFLAGS=-D_FILE_OFFSET_BITS=64 -I$(top_srcdir)/inc/opx -I$(includedir)/opx
libopx_nas_qos_la_CXXFLAGS=-std=c++11
libopx_nas_qos_la_LDFLAGS=-shared -version-info 1:1:0
libopx_nas_qos_la_LIBADD=-lopx_logging -lopx_nas_ndi -lopx_cps_api_common -lopx_common -lopx_nas_common

systemdconfdir=/lib/systemd/system
systemdconf_DATA = scripts/init/*.service
