#!/usr/bin/env python

import re
import time

from cmd_process import get_stdout

#------ RTK_P2P_WFD_Driver,Process cmd ------
cmd_kill_hostapd			= 'killall hostapd'
cmd_iwlist_wlan0_scan			= 'iwlist wlan0 scan'
cmd_iwpriv_wlan0_get_peer_ifa		= 'iwpriv wlan0 p2p_get peer_ifa'
cmd_iwpriv_wlan0_set_disable		= 'iwpriv wlan0 p2p_set enable=0'
cmd_iwpriv_wlan0_set_enable		= 'iwpriv wlan0 p2p_set enable=1'
cmd_iwpriv_wlan0_set_intent		= 'iwpriv wlan0 p2p_set inetent=15'
cmd_iwpriv_wlan0_set_op_ch		= 'iwpriv wlan0 p2p_set op_ch=11'
cmd_iwpriv_wlan0_set_ssid		= 'iwpriv wlan0 p2p_set ssid=DIRECT-RT'		
cmd_iwpriv_wlan0_set_DN			= 'iwpriv wlan0 p2p_set setDN=Piracast'
cmd_iwpriv_wlan0_get_status		= 'iwpriv wlan0 p2p_get status'
cmd_iwpriv_wlan0_get_peer_deva		= 'iwpriv wlan0 p2p_get peer_deva'
cmd_iwpriv_wlan0_get_got_wpsinfo	= 'iwpriv wlan0 p2p_get got_wpsinfo=3'
cmd_iwpriv_wlan0_get_req_cm		= 'iwpriv wlan0 p2p_get req_cm'
cmd_iwpriv_wlan0_get_role		= 'iwpriv wlan0 p2p_get role'
cmd_iwpriv_wlan0_get_peer_ifa		= 'iwpriv wlan0 p2p_get peer_ifa'
cmd_hostapd_run_in_background		= './hostapd -B p2p_hostapd.conf'
cmd_hostapd_cli_wps_pbc			= './hostapd_cli wps_pbc any'
cmd_hostapd_cli_hostapd_reported	= './hostapd_cli all_sta'

# --------------------------------------------
#   p2p_enable
#	Enable wifi direct
#---------------------------------------------
def p2p_enable():
	# Enable p2p
	get_stdout(cmd_iwpriv_wlan0_set_enable)
	
	# Set intent
	get_stdout(cmd_iwpriv_wlan0_set_intent)	
	
	# Set operation channel
	get_stdout(cmd_iwpriv_wlan0_set_op_ch)
	
	# Sleep for 50ms
	time.sleep(0.05)	

	# Set ssid
	get_stdout(cmd_iwpriv_wlan0_set_ssid)

	# Set Device Name
	get_stdout(cmd_iwpriv_wlan0_set_DN)


# --------------------------------------------
#   p2p_status_get
#	get p2p current status
#---------------------------------------------
def p2p_status_get():
	output = get_stdout(cmd_iwpriv_wlan0_get_status)
	match = re.search(r'Status=(\d*)',output)
	return int(match.group(1))


# --------------------------------------------
#   p2p_peer_scan
#	find out nearny p2p device
#---------------------------------------------
def p2p_peer_scan():
	count = 0
	
	while 1:
		output = get_stdout(cmd_iwlist_wlan0_scan)
		print output
		
		if 'No scan results' not in output:
			return True
		
		if count > 3:
			return False

		count += 1

# --------------------------------------------
#   p2p_set_nego(mac)
#	find out nearny p2p device
#---------------------------------------------
def p2p_set_nego(mac):
	print 'p2p+set_nego:'
	print 'mac: %s' % mac
	get_stdout('iwpriv wlan0 p2p_set nego=%s' %mac)
	
	# Enter negotiation loop
	while 1:
		
		# Wait for result
		time.sleep(0.5)
		
		# Poll status
		p2p_status = p2p_status_get()
		print 'peer_status: %d' % p2p_status

		# For windows 8.1 support ,we consider 19 as negotiation completed
		if peer_status in [10, 19]:
			print 'Negotiation suceeded!'
			break

	# Get role
	print 'p2p_role_get:'
	output = get_stdout(cmd_iwpriv_wlan0_get_role)
	match = re.search(r'Role=(\d*)', output)
	role = int(match.group(1))
	print 'Role: %s' % role

	# Get peer interface address
	print 'p2p_peer_interface address_get'
	output = get_stdout(cmd_iwpriv_wlan0_get_peer_ifa)	
	print output
	match = re.search(r'MAC (.*)$', output)
	peer_ifa = match.group(1)	
	print 'peer_ifa: %s' % peer_ifa




# -------------------------------------------- 
#   wfd_p2p_connection_wait 
#	wfd p2p connection main control
#---------------------------------------------
def wfd_p2p_connection_wait(): 
	get_stdout(cmd_kill_hostapd)
	
	# Disable p2p
	get_stdout(cmd_iwpriv_wlan0_set_disable)

	time.sleep(0.5)

	# Enable p2p
	p2p_enable()	
	
	print 'Waiting for incoming connection...'

	while 1:

		# Poll status
		p2p_status = p2p_status_get()
		print 'p2p_status : %d' % p2p_status

		# status 8 is the original Discovery Request
		# status 22 needs to be handled this way, or Nexus 4 4.4 won't always work
		# status 19 was added to try to implement windows 8.1 support
		# Check whether discovery request received and peer scan results found
		if p2p_status in [8, 19, 22]:
			
			print 'Discovery request received!'
			peer_found = p2p_peer_scan()	
			if peer_found :
				break	

			# Disable p2p
			get_stdout(cmd_iwpriv_wlan0_set_disable)
			
		time.sleep(1)

	# Get peer device address
	print 'Getting peer device address...'	
	print 'p2p_peer_devaddr_get:'
	output = get_stdout(cmd_iwpriv_wlan0_get_peer_deva)
	print output 
	match = re.search(r'\n(.*)$', output)
	print match
	mac = ':'.join(re.findall('..', match.group(1))) 
	print 'peer_devaddr: %s' % mac

	# Notify received wps info
	print 'p2p_wpsinfo:'
	get_stdout(cmd_iwpriv_wlan0_get_got_wpsinfo)
	print 'Getting peer authentiacation type...'
	
	# Get request configiration
	print 'p2p_req_cn_get:'
	get_stdout(cmd_iwpriv_wlan0_get_req_cm)	
	print 'COnfirm peer authentication' 

	# Set p2p negotiation
	p2p_set_nego(mac)

	# Rule = 3 ,it means this p2p device should be the 802.11 AP role,
 	# and launch the hostapd to enable the SoftAP functionality and enable the WPS procedure
	
	# Start hostapd and wait for it to daemonize; ignore stdout	
	print 'hostapd_run_in_background'
	get_stdout(cmd_hostapd_run_in_background)
	# Wait for initialization
	time.sleep(1)		
																						
	# hostapd enable WPS procedure
	print 'hostapd_enable WPS procedure'
	while 1:
		print 'do_wps:'
		output = get_stdout(cmd_hostapd_cli_wps_pbc)
		print output

		if 'OK' in output:
			print 'wps passed!!'
			break
	
		time.sleep(1)

	# Wait fpr hostapd enable WPS procedure
	time.sleep(1)

	# Check whether p2p connection is established successfully and DHCP Server start automatically
	# if result is 'dot11RSNAStatsSTAAddress=xx:xx:xx:xx:xx:xx',it means works fine
	while 1:
		print 'read_all_sta:'
		output = get_stdout(cmd_hostapd_cli_hostapd_reported)
		if 'dot11RSNAStatsSTAAddress' in output :
			print 'Wirless display negotiation completed!'
			break

		time.sleep(1)	
