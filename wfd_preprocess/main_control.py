#!/usr/bin/env python

import os
import re
import time

import wfd
import sink_nego
from cmd_process import get_stdout

#---------------------- Interface,Process,Service cmd ---- 

cmd_wlan0_up = 'ifup wlan0'
cmd_inc_rmem_default = 'sysctl -w net.core.rmem_default=1000000'
cmd_kill_core_app = 'killall core'
cmd_dhcp_start = 'service isc-dhcp-server start'
cmd_dhcp_stio = 'service isc-dhcp-server stop'

#---------------------- dhcp.leases file path ------------
dhcp_lease_file = '/var/lib/dhcp/dhcp.leases'


def lease_file_timestamp_get():
	return get_stdout('ls -l "%s"' % dhcp_lease_file)


#---------------------- get the leased IP address---------
def leased_ip_get():
	contents = open(dhcp_lease_file).read()
	ip_list = re.findall(r'lease (\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})', contents)
	
	# return the most recently leased IP address
	retrun ip_list[-1]

#----------------------- main control start --------------

print 'Bring up wlan0 just in case...'
get_stdout(cmd_wlan0_up)

print 'Increase rmem_default...'
get_stdout(cmd_inc_rmem_default)

while 1:
	
	# start DHCP
	print get_stdout(cmd_dhcp_start)

	# get previous timestamp
	prev_ts = lease_file_timestamp_get()
	
	# wait for wifi p2p connection, dhcp server launch and be ready to provide the IP address to DHCP client
	wfd.wfd_p2p_connection_wait()

	# wait until DHCP lease file is updated
	while 1:

		curr_ts = lease_file_timestamp_get()
		
		# check whether DHCP lease file has been updated
		if curr_ts != prev_ts:

			print 'WiFi Display Source has requested IP!'
	
			# wait for network to be properly configured
			time.sleep(2)
			break
		
		print 'DHCP lease table has not been updated, wait for a second...'

		time.sleep(1)

	# get WiFi Display Source IP address
	ip = leased_ip_get() 
	
	print 'WiFi Display Source leased IP: ', ip

	# connect to WiFi Display Source and wfd negotiation
	sink_nego.source_connect(ip)

	# stop DHCP
	print get_stdout(cmd_dhcp_stop)

	# kill main control app
	print get_stdout(cmd_kill_core_app)


