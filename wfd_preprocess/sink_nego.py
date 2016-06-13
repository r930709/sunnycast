#!/usr/bin/env python

import re 
import socket


# --------------------------------------------
#   source_connect(ip)
#       TCP socket create and negotiate by exchange RTSP message
#---------------------------------------------
def source_connect(ip):

	# Create a socket object
	s = socket.socket()
	 
	# Connect to wfd default port which is provided by wfd Source(TCP server role).
	s.connect((ip, 7236))

	# Connect via wireless interface
	s.setsockopt(socket.SOL_SOCKET, 25, 'wlan0')
	
	# Wait to receive m1 request from wfd source
	m1_req = s.recv(1024)
	print 'M1 Req from wfd source : %s' %  repr(m1_req)

	# Send m1 response to wfd source
	s.send(m1_resp)
	print 'M1 Resp to wfs source : %s' % repr(m1_resp)
		
	# Wait to receive m2 request from wfd source
	m2_rsp = s.recv(1024)
	print 'M2 Resp to wfs source : %s' % repr(m2_resp)


