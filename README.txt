/*******************************************************************************
	
Authors:	Jeremy Tsang, Kevin Eng		
	
Date:		March 22, 2014

Purpose:	COMP 8005 Assignment 3 - Basic Application Level Port Forwarder

*******************************************************************************/

##############################
Host Server: port_forwarder.c
##############################
To compile:
gcc -Wall -o pf port_forwarder.c -lpthread

Usage:
./pf

#######################################
Configuration File: port_forwarder.conf
#######################################    
Usage:
On a new line, add an entry in the following format:
listeningPort,forwardingIPaddress,forwardingPort

Example:
80,192.168.1.25,8005
