/* 
  SZARP: SCADA software 

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
/*
 * 
 * Darek Marcinkiewicz <reksio@newterm.pl>
 * 
 */

/*
 Daemon description block.

 @description_start

 @class 4

 @devices All devices or SCADA systems (or other software) using Modbus protocol.
 @devices.pl Wszystkie urz�dzenia i systemy SCADA (lub inne oprogramowanie) wykorzystuj�ce protok� Modbus.

 @protocol Modbus RTU using serial line or Modbus TCP/IP.
 @protocol.pl Modbus RTU (po��czenie szeregowe RS232/RS485) lub Modbus TCP/IP.

 @config Daemon is configured in params.xml, all attributes from 'modbus' namespace not explicity
 described as optional are required.
 @config.pl Sterownik jest konfigurowany w pliku params.xml, wszystkie atrybuty z przestrzeni nazw
 'modbus', nie opisane jako opcjonalne, s� wymagane.

 @comment This daemon is to replace older mbrtudmn and mbtcpdmn daemons.
 @comment.pl Ten sterownik zast�puje starsze sterowniki mbrtudmn i mbtcpdmn.

 @config_example
<device 
     xmlns:modbus="http://www.praterm.com.pl/SZARP/ipk-extra"
     daemon="/opt/szarp/bin/mbdmn" 
     path="/dev/null"
     modbus:daemon-mode=
 		allowed modes are 'tcp-server' and 'tcp-client' and 'serial-client' and 'serial-server'
     modbud:tcp-port="502"
		TCP port we are listenning on/connecting to (server/client)
     modbus:tcp-allowed="192.9.200.201 192.9.200.202"
		(optional) list of allowed clients IP addresses for server mode, if empty all
		addresses are allowed
     modbus:tcp-address="192.9.200.201"
 		server IP address (required in client mode)
     modbus:tcp-keepalive="yes"
 		should we set TCP Keep-Alive options? "yes" or "no"
     modbus:tcp-timeout="32"
		(optional) connection timeout in seconds, after timeout expires, connection
		is closed; default empty value means no timeout
     modbus:nodata-timeout="15"
		(optional) timeout (in seconds) to set data to 'NO_DATA' if data is not available,
		default is 20 seconds
     modbus:nodata-value="-1"
 		(optional) value to send instead of 'NO_DATA', default is 0
     modbus:FloatOrder="msblsb"
 		(optional) registers order for 4 bytes (2 registers) float order - "msblsb"
		(default) or "lsbmsb"; values names are a little misleading, it shoud be 
		msw/lsw (most/less significant word) not msb/lsb (most/less significant byte),
		but it's left like this for compatibility with Modbus RTU driver configuration
      >
      <unit id="1" modbus:id="49">
		modbus:id is optional Modbus unit identifier - number from 0 to 255; default is to
		use IPK id attribute, but parsed as a character value and not a number; in this 
		example both definitions id="1" and modbud:id="49" give tha same value, because ASCII
		code of "1" is 49
              <param
			Read value using ReadHoldingRegisters (0x03) Modbus function              
                      name="..."
                      ...
                      modbus:address="0x03"
 	                      	modbus register number, starting from 0
                      modbus:val_type="integer">
 	                      	register value type, 'integer' (2 bytes, 1 register) or float (4 bytes,
	                       	2 registers)
                      ...
              </param>
              <param
                      name="..."
                      ...
                      modbus:address="0x04"
                      modbus:val_type="float"
			modbus:val_op="msblsb"
			modbus:val_op="LSW">
				(optional) operator for converting data from float to 2 bytes integer;
				default is 'NONE' (simple conversion to short int), other values
				are 'LSW' and 'MSW' - converting to 4 bytes long and getting less/more
				significant word; in this case there should be 2 parameters with the
				same register address and different val_op attributes - LSW and MSW.
				s�owa
                      ...
              </param>
              ...
              <send 
              		Sending value using WriteMultipleRegisters (0x10) Modbus function
                      param="..." 
                      type="min"
                      modbus:address="0x1f"
                      modbus:val_type="float">
                      ...
              </send>
              ...
      </unit>
 </device>

 @description_end

*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <event.h>
#include <vector>

#include <boost/lexical_cast.hpp>

#include <iostream>
#include <deque>
#include <set>

#include <libxml/parser.h>

#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "conversion.h"
#include "ipchandler.h"
#include "liblog.h"
#include "xmlutils.h"
#include "tokens.h"
#include "borutadmn.h"

bool g_debug = false;

/*implementation*/

std::string sock_addr_to_string(struct sockaddr_in& addr) {
	return inet_ntoa(addr.sin_addr);
}

int set_nonblock(int fd) {

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) {
		dolog(0, "set_nonblock: Unable to get socket flags");
		return -1;
	}

	flags |= O_NONBLOCK;
	flags = fcntl(fd, F_SETFL, flags);
	if (flags == -1) {
		dolog(0, "set_nonblock: Unable to set socket flags");
		return -1;
	}

	return 0;
}

bool operator==(const sockaddr_in &s1, const sockaddr_in &s2) {
	return s1.sin_addr.s_addr == s2.sin_addr.s_addr && s1.sin_port == s2.sin_port;
}

int get_serial_port_config(xmlNodePtr node, serial_port_configuration &spc) {

	std::string parity;
	get_xml_extra_prop(node, "parity", parity, true);
	if (parity.empty()) {
		dolog(10, "Serial port configuration, parity not specified, assuming no parity");
		spc.parity = serial_port_configuration::NONE;
	} else if (parity == "none") {
		dolog(10, "Serial port configuration, none parity");
		spc.parity = serial_port_configuration::NONE;
	} else if (parity == "even") {
		dolog(10, "Serial port configuration, even parity");
		spc.parity = serial_port_configuration::EVEN;
	} else if (parity == "odd") {
		dolog(10, "Serial port configuration, odd parity");
		spc.parity = serial_port_configuration::ODD;
	} else {
		dolog(0, "Unsupported parity %s, confiugration invalid (line %ld)!!!", parity.c_str(), xmlGetLineNo(node));
		return 1;	
	}

	std::string stop_bits;
	get_xml_extra_prop(node, "stopbits", stop_bits, true);
	if (stop_bits.empty()) {
		dolog(10, "Serial port configuration, stop bits not specified, assuming 1 stop bit");
		spc.stop_bits = 1;
	} else if (stop_bits == "1") {
		dolog(10, "Serial port configuration, setting one stop bit");
		spc.stop_bits = 1;
	} else if (stop_bits == "2") {
		dolog(10, "Serial port configuration, setting two stop bits");
		spc.stop_bits = 2;
	} else {
		dolog(0, "Unsupported number of stop bits %s, confiugration invalid (line %ld)!!!", stop_bits.c_str(), xmlGetLineNo(node));
		return 1;
	}

#if 0
	xmlChar* delay_between_chars = get_device_node_prop(xp_ctx, "out-intra-character-delay");
	std::string 
	if (delay_between_chars == NULL) {
		dolog(10, "Serial port configuration, delay between chars not given assuming no delay");
		spc.delay_between_chars = 0;
	} else {
		spc.delay_between_chars = atoi((char*) delay_between_chars) * 1000;
		dolog(10, "Serial port configuration, delay between chars set to %d miliseconds", spc.delay_between_chars);
	}
	xmlFree(delay_between_chars);

	xmlChar* read_timeout = get_device_node_prop(xp_ctx, "read-timeout");
	if (read_timeout == NULL) {
		dolog(10, "Serial port configuration, read timeout not given, will use one based on speed");
		spc.read_timeout = 0;
	} else {
		spc.read_timeout = atoi((char*) read_timeout) * 1000;
		dolog(10, "Serial port configuration, read timeout set to %d miliseconds", spc.read_timeout);
	}
	xmlFree(read_timeout);
#endif

	return 0;
}

int set_serial_port_settings(int fd, serial_port_configuration &spc) {
	struct termios ti;
	if (tcgetattr(fd, &ti) == -1) {
		dolog(1, "Cannot retrieve port settings, errno:%d (%s)", errno, strerror(errno));
		return 1;
	}

	dolog(8, "setting port speed to %d", spc.speed);
	switch (spc.speed) {
		case 300:
			ti.c_cflag = B300;
			break;
		case 600:
			ti.c_cflag = B600;
			break;
		case 1200:
			ti.c_cflag = B1200;
			break;
		case 2400:
			ti.c_cflag = B2400;
			break;
		case 4800:
			ti.c_cflag = B4800;
			break;
		case 9600:
			ti.c_cflag = B9600;
			break;
		case 19200:
			ti.c_cflag = B19200;
			break;
		case 38400:
			ti.c_cflag = B38400;
			break;
		default:
			ti.c_cflag = B9600;
			dolog(8, "setting port speed to default value 9600");
	}

	if (spc.stop_bits == 2)
		ti.c_cflag |= CSTOPB;

	switch (spc.parity) {
		case serial_port_configuration::EVEN:
			ti.c_cflag |= PARENB;
			break;
		case serial_port_configuration::ODD:
			ti.c_cflag |= PARODD;
			break;
		case serial_port_configuration::NONE:
			break;
	}
			
	ti.c_oflag = 
	ti.c_iflag =
	ti.c_lflag = 0;

	ti.c_cflag |= CS8 | CREAD | CLOCAL ;

	if (tcsetattr(fd, TCSANOW, &ti) == -1) {
		dolog(1,"Cannot set port settings, errno: %d (%s)", errno, strerror(errno));	
		return -1;
	}
	return 0;
}

void boruta_driver::set_event_base(struct event_base* ev_base)
{
	m_event_base = ev_base;
}

std::pair<size_t, size_t> & client_driver::id() {
	return m_id;
}

size_t& server_driver::id() {
	return m_id;
}

void tcp_client_driver::set_manager(tcp_client_manager* manager) {
	m_manager = manager;
}

void serial_client_driver::set_manager(serial_client_manager* manager) {
	m_manager = manager;
}

protocols::protocols() {
	m_tcp_client_factories["zet"] = create_zet_tcp_client;
	m_serial_client_factories["zet"] = create_zet_serial_client;
#if 0
	m_tcp_client_factories["modbus"] = create_modbus_tcp_client;
	m_serial_client_factories["modbus"] = create_modbus_serial_client;
	m_tcp_server_factories["modbus"] = create_modbus_tcp_server;
	m_serial_server_factories["modbus"] = create_modbus_serial_server;
#endif
}

std::string protocols::get_proto_name(xmlNodePtr node) {
	std::string ret;
	get_xml_extra_prop(node, "proto", ret);
	return ret;
}

tcp_client_driver* protocols::create_tcp_client_driver(xmlNodePtr node) {
	std::string proto = get_proto_name(node);
	if (proto.empty())
		return NULL;
	tcp_client_factories_table::iterator i = m_tcp_client_factories.find(proto);
	if (i == m_tcp_client_factories.end()) {
		dolog(0, "No driver defined for proto %s and tcp client role", proto.c_str());
		return NULL;
	}
	return i->second();
}

serial_client_driver* protocols::create_serial_client_driver(xmlNodePtr node) {
	std::string proto = get_proto_name(node);
	if (proto.empty())
		return NULL;
	serial_client_factories_table::iterator i = m_serial_client_factories.find(proto);
	if (i == m_serial_client_factories.end()) {
		dolog(0, "No driver defined for proto %s and serial client role", proto.c_str());
		return NULL;
	}
	return i->second();
}

tcp_server_driver* protocols::create_tcp_server_driver(xmlNodePtr node) {
	std::string proto = get_proto_name(node);
	if (proto.empty())
		return NULL;
	tcp_server_factories_table::iterator i = m_tcp_server_factories.find(proto);
	if (i == m_tcp_server_factories.end()) {
		dolog(0, "No driver defined for proto %s and tcp server role", proto.c_str());
		return NULL;
	}
	return i->second();
}

serial_server_driver* protocols::create_serial_server_driver(xmlNodePtr node) {
	std::string proto = get_proto_name(node);
	if (proto.empty())
		return NULL;
	serial_server_factories_table::iterator i = m_serial_server_factories.find(proto);
	if (i == m_serial_server_factories.end()) {
		dolog(0, "No driver defined for proto %s and serial server role", proto.c_str());
		return NULL;
	}
	return i->second();
}

serial_connection::serial_connection(size_t _conn_no, serial_connection_manager *_manager) :
	conn_no(_conn_no), manager(_manager), fd(-1), bufev(NULL) {}

int serial_connection::open_connection(const std::string& path, struct event_base* ev_base) {
	fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK, 0);
	if (fd == -1) {
		dolog(0, "Failed do open port: %s, error: %s", path.c_str(), strerror(errno));
		return 1;
	}
	bufev = bufferevent_new(fd, connection_read_cb, NULL, connection_error_cb, this);
	bufferevent_base_set(ev_base, bufev);
	bufferevent_enable(bufev, EV_READ | EV_WRITE | EV_PERSIST);
	return 0;
}

void serial_connection::close_connection() {
	if (bufev) {
		bufferevent_free(bufev);
		bufev = NULL;
	}
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}	
}

void serial_connection::connection_read_cb(struct bufferevent *ev, void* _connection) {
	serial_connection* c = (serial_connection*) _connection;
	c->manager->connection_read_cb(c);
}

void serial_connection::connection_error_cb(struct bufferevent *ev, short event, void* _connection) {
	serial_connection* c = (serial_connection*) _connection;
	c->manager->connection_error_cb(c);
}

tcp_client_manager::tcp_connection::tcp_connection(tcp_client_manager *_manager, size_t _addr_no) : state(NOT_CONNECTED), fd(-1), bufev(NULL), conn_no(_addr_no), manager(_manager) {}

int tcp_client_manager::configure_tcp_address(xmlNodePtr node, struct sockaddr_in &addr) {
	std::string address;
	if (get_xml_extra_prop(node, "tcp-address", address))
		return 1;
	if (!inet_aton(address.c_str(), &addr.sin_addr)) {
		dolog(0, "Invalid tcp-address value(%s) in line %ld, exiting", address.c_str(), xmlGetLineNo(node));
		return 1;
	}
	int port;
	if (get_xml_extra_prop(node, "tcp-port", port))
		return 1;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	return 0;
}

void tcp_client_manager::close_connection(tcp_connection &c) {
	if (c.bufev) {
		bufferevent_free(c.bufev);
		c.bufev = NULL;
	}
	if (c.fd >= 0) {
		close(c.fd);
		c.fd = -1;
	}
	c.state = tcp_connection::NOT_CONNECTED;
}

int tcp_client_manager::open_connection(tcp_connection &c, struct sockaddr_in& addr) {
	c.fd = socket(PF_INET, SOCK_STREAM, 0);
	assert(c.fd >= 0);
	if (set_nonblock(c.fd)) {
		dolog(1, "Failed to set non blocking mode on socket");
		close_connection(c);
		return 1;
	}
do_connect:
	int ret = connect(c.fd, (struct sockaddr*) &addr, sizeof(addr));
	if (ret == 0) {
		c.state = tcp_connection::CONNECTED;
	} else if (errno == EINTR) {
		goto do_connect;
	} else if (errno == EWOULDBLOCK || errno == EINPROGRESS) {
		c.state = tcp_connection::CONNECTING;
	} else {
		dolog(0, "Failed to connect: %s", strerror(errno));
		close_connection(c);
		return 1;
	}
	c.bufev = bufferevent_new(c.fd, connection_read_cb, connection_write_cb, connection_error_cb, &c);
	bufferevent_base_set(m_boruta->get_event_base(), c.bufev);
	bufferevent_enable(c.bufev, EV_READ | EV_WRITE | EV_PERSIST);
	return 0;
}

void tcp_client_manager::connect_all() {
	for (size_t i = 0; i < m_connections.size(); i++) {
		if (m_connections[i].state == tcp_connection::CONNECTED)
			continue;
		if (m_connections[i].state == tcp_connection::CONNECTING) 
			//TODO: probably we can do better
			continue;
		if (open_connection(m_connections[i], m_addresses[i]))
			dolog(1, "Failed to establish connection with: %s", sock_addr_to_string(m_addresses[i]).c_str());
	}
}

int tcp_client_manager::configure(TUnit *unit, xmlNodePtr node, short* read, short* send, protocols& _protocols) {
	struct sockaddr_in addr;
	if (configure_tcp_address(node, addr))
		return 1;
	tcp_client_driver* driver = _protocols.create_tcp_client_driver(node);
	if (driver == NULL)
		return 1;
	if (driver->configure(unit, node, read, send)) {
		delete driver;
		return 1;
	}
	size_t i;
	for (i = 0; i < m_addresses.size(); i++)
		if (addr == m_addresses[i])
			break;
	if (i == m_addresses.size()) {
		i = m_addresses.size();
		m_addresses.push_back(addr);
		m_tcp_clients.push_back(std::vector<tcp_client_driver*>());
		m_connections.push_back(tcp_connection(this, i));
	}
	driver->id() = std::make_pair(i, m_tcp_clients[i].size());
	m_tcp_clients[i].push_back(driver);
	driver->set_manager(this);
	return 0;
}

int tcp_client_manager::initialize() {
	for (size_t i = 0; i < m_tcp_clients.size(); i++)
		m_current_tcp_client.at(i) = m_tcp_clients.at(i).size();
	for (size_t i = 0; i < m_tcp_clients.size(); i++)
		for (size_t j = 0; j < m_tcp_clients[i].size(); j++)
			m_tcp_clients[i][j]->set_event_base(m_boruta->get_event_base());
	return 0;
}

void tcp_client_manager::starting_new_cycle() {
	dolog(10, "tcp_client_manager, starting new cycle");
	connect_all(); 
	for (size_t i = 0; i < m_addresses.size(); i++) {
		if (m_connections.at(i).state == tcp_connection::NOT_CONNECTED) {
			for (size_t j = 0; m_tcp_clients.at(i).size(); j++)
				m_tcp_clients.at(i).at(j)->connection_error(m_connections.at(i).bufev);
		} else {
			size_t& j = m_current_tcp_client.at(i);
			if (j == m_tcp_clients.at(i).size()) {
				j = 0;
				if (m_connections.at(i).state == tcp_connection::CONNECTED)
					m_tcp_clients.at(i).at(j)->scheduled(m_connections.at(i).bufev);
			}
		}
	}
}

void tcp_client_manager::driver_finished_job(tcp_client_driver *driver) {
	size_t i = driver->id().first;
	size_t& j = m_current_tcp_client.at(i);
	assert(driver->id().second == j);
	if (++j < m_tcp_clients.at(i).size())
		m_tcp_clients.at(i).at(j)->scheduled(m_connections.at(i).bufev);
}

void driver_(tcp_client_driver *driver);

void tcp_client_manager::connection_read_cb(struct bufferevent *ev, void* _tcp_connection) {
	tcp_connection* c = (tcp_connection*) _tcp_connection;
	tcp_client_manager* t = c->manager;
	tcp_client_driver* d = t->m_tcp_clients.at(c->conn_no).at(t->m_current_tcp_client[c->conn_no]);
	d->data_ready(ev);
}

void tcp_client_manager::connection_write_cb(struct bufferevent *ev, void* _tcp_connection) {
	tcp_connection* c = (tcp_connection*) _tcp_connection;
	if (c->state != tcp_connection::CONNECTING)
		return;
	tcp_client_manager* t = c->manager;
	c->state = tcp_connection::CONNECTED;
	tcp_client_driver* d = t->m_tcp_clients.at(c->conn_no).at(t->m_current_tcp_client.at(c->conn_no));
	d->scheduled(ev);
}
 
void tcp_client_manager::connection_error_cb(struct bufferevent *ev, short event, void* _tcp_connection) {
	tcp_connection* c = (tcp_connection*) _tcp_connection;
	tcp_client_manager* t = c->manager;
	t->close_connection(*c);
	size_t& cc = t->m_current_tcp_client.at(c->conn_no);
	client_driver* d = t->m_tcp_clients.at(c->conn_no).at(cc);
	d->connection_error(c->bufev);
	while (++cc < t->m_tcp_clients.at(c->conn_no).size()) {
		tcp_client_driver* d = t->m_tcp_clients.at(c->conn_no).at(cc);
		if (t->open_connection(*c, t->m_addresses.at(c->conn_no)) == 0) {
			if (c->state == tcp_connection::CONNECTED)
				d->scheduled(c->bufev);
			return;
		}
		d->connection_error(c->bufev);
		t->close_connection(*c);
	}
}

void serial_client_manager::schedule(size_t connection_number) {
	serial_connection& sc = m_connections.at(connection_number);
	size_t j = m_current_serial_client.at(connection_number);
	set_serial_port_settings(sc.fd, m_configurations.at(connection_number).at(j));
	m_clients.at(connection_number).at(j)->scheduled(
			m_connections.at(connection_number).bufev,
			m_connections.at(connection_number).fd);
}

int serial_client_manager::configure(TUnit *unit, xmlNodePtr node, short* read, short* send, protocols &_protocols) {
	serial_port_configuration spc;
	if (!get_serial_port_config(node, spc))
		return 1;
	serial_client_driver* driver = _protocols.create_serial_client_driver(node);
	if (driver == NULL)
		return 1;
	if (driver->configure(unit, node, read, send)) {
		delete driver;
		return 1;
	}
	std::map<std::string, size_t>::iterator i = m_ports_client_no_map.find(spc.path);
	size_t j;	
	if (i == m_ports_client_no_map.end()) {
		j = m_clients.size();
		m_clients.push_back(std::vector<serial_client_driver*>());
		m_configurations.resize(m_configurations.size() + 1);
		m_ports_client_no_map[spc.path] = j;
		m_connections.push_back(serial_connection(j, this));
	} else {
		j = i->second;
	}
	m_clients.at(j).push_back(driver);
	m_configurations.at(j).push_back(spc);
	driver->id() = std::make_pair(j, m_clients.at(j).size());
	driver->set_manager(this);
	return 0;
}

int serial_client_manager::initialize() {
	for (size_t i = 0; i < m_clients.size(); i++)
		m_current_serial_client.at(i) = m_clients.at(i).size();
	for (size_t i = 0; i < m_clients.size(); i++)
		for (size_t j = 0; j < m_clients[i].size(); j++)
			m_clients[i][j]->set_event_base(m_boruta->get_event_base());
	return 0;
}

void serial_client_manager::starting_new_cycle() {
	dolog(10, "serial_client_manager, starting new cycle");
	for (size_t i = 0; i < m_clients.size(); i++) {
		if (m_connections.at(i).fd < 0) {
			if (m_connections.at(i).open_connection(
					m_configurations.at(i).at(m_current_serial_client.at(i)).path,
					m_boruta->get_event_base())) {
				for (size_t j = 0; m_clients.at(i).size(); j++)
					m_clients.at(i).at(j)->connection_error(m_connections.at(i).bufev);
				continue;
			}
		}
		size_t& j = m_current_serial_client.at(i);
		if (j == m_clients.at(i).size()) {
			j = 0;
			schedule(i);
		}
	}
}

void serial_client_manager::driver_finished_job(serial_client_driver *driver) {
	size_t i = driver->id().first;
	size_t& j = m_current_serial_client.at(i);
	assert(driver->id().second == j);
	if (++j < m_clients.at(i).size())
		schedule(i);
}

void serial_client_manager::connection_read_cb(serial_connection* c) {
	serial_client_driver *d = m_clients.at(c->conn_no).at(m_current_serial_client.at(c->conn_no));
	d->data_ready(c->bufev, c->fd);
}

void serial_client_manager::connection_error_cb(serial_connection* c) {
	size_t &cc = m_current_serial_client.at(c->conn_no);
	serial_client_driver *d = m_clients.at(c->conn_no).at(cc);
	d->connection_error(c->bufev);
	c->close_connection();
	size_t cs = m_clients.at(c->conn_no).size();
	if (++cc < cs) {
		if (c->open_connection(m_configurations.at(c->conn_no).at(cc).path, m_boruta->get_event_base())) {
			do {
				m_clients.at(c->conn_no).at(cc)->connection_error(c->bufev);
			} while (++cc < cs);
		} else {
			schedule(cc);
		}
	}
}

int serial_server_manager::configure(TUnit *unit, xmlNodePtr node, short* read, short* send, protocols &_protocols) {
	serial_port_configuration spc;
	if (get_serial_port_config(node, spc))
		return 1;
	serial_server_driver* driver = _protocols.create_serial_server_driver(node);
	if (driver == NULL)
		return 1;
	if (driver->configure(unit, node, read, send)) {
		delete driver;
		return 1;
	}
	driver->id() = m_drivers.size();
	m_drivers.push_back(driver);
	m_connections.push_back(serial_connection(m_connections.size(), this));
	m_configurations.push_back(spc);
	return 0;
}

int serial_server_manager::initialize() {
	for (size_t i = 0; i < m_drivers.size(); i++)
		m_drivers[i]->set_event_base(m_boruta->get_event_base());
	return 0;
}

void serial_server_manager::starting_new_cycle() {
	for (size_t i = 0; i < m_connections.size(); i++)
		if (m_connections[i].fd < 0) {
			if (m_connections[i].open_connection(m_configurations.at(i).path, m_boruta->get_event_base()))
				m_connections[i].close_connection();
			else
				set_serial_port_settings(m_connections[i].fd, m_configurations.at(i));
		}
	for (std::vector<serial_server_driver*>::iterator i = m_drivers.begin();
			i != m_drivers.end();
			i++)
		(*i)->starting_new_cycle();
}

void serial_server_manager::restart_connection_of_driver(serial_server_driver* driver) {
	std::vector<serial_server_driver*>::iterator i = std::find(m_drivers.begin(), m_drivers.end(), driver);		
	assert(i != m_drivers.end());
	m_connections.at(std::distance(i, m_drivers.end())).close_connection();
}

void serial_server_manager::connection_read_cb(serial_connection *c) {
	m_drivers.at(c->conn_no)->data_ready(c->bufev);
}

void serial_server_manager::connection_error_cb(serial_connection *c) {
	m_drivers.at(c->conn_no)->connection_error(c->bufev);
	m_connections.at(c->conn_no).close_connection();
}

tcp_server_manager::listen_port::listen_port(tcp_server_manager* _manager, int _port, int _serv_no) :
	manager(_manager), port(_port), fd(-1), serv_no(_serv_no) {}

int tcp_server_manager::start_listening_on_port(int port) {
	int ret, fd, on = 1;
	struct sockaddr_in addr;
	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		dolog(0, "socket() failed, errno %d (%s)", errno, strerror(errno));
		return -1;
	}
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
			&on, sizeof(on));
	if (ret == -1) {
		dolog(0, "setsockopt() failed, errno %d (%s)",
				errno, strerror(errno));
		close(fd);
		return -1;
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret == 0) {
		return fd;
	} else {
		dolog(1, "bind() failed, errno %d (%s), will retry",
			errno, strerror(errno));
		close(fd);
		return -1;
	}
}

int tcp_server_manager::configure(TUnit *unit, xmlNodePtr node, short* read, short* send, protocols &_protocols) {
	tcp_server_driver* driver = _protocols.create_tcp_server_driver(node);
	if (driver == NULL)		
		return 1;
	if (driver->configure(unit, node, read, send)) {
		delete driver;
		return 1;
	}
	int port;
	if (!get_xml_extra_prop(node, "port", port))
		return 1;
	m_listen_ports.push_back(listen_port(this, port, m_drivers.size()));
	driver->id() = m_drivers.size();
	m_drivers.push_back(driver);
	return 0;
}

int tcp_server_manager::initialize() {
	for (size_t i = 0; i < m_drivers.size(); i++)
		m_drivers[i]->set_event_base(m_boruta->get_event_base());
	return 0;
}

void tcp_server_manager::starting_new_cycle() {
	for (std::vector<listen_port>::iterator i = m_listen_ports.begin(); i != m_listen_ports.end(); i++) {
		if (i->fd >= 0)
			continue;
		i->fd = start_listening_on_port(i->port);
		if (i->fd < 0)
			continue;
		event_set(&i->_event, i->fd, EV_READ | EV_PERSIST, connection_accepted_cb, &(*i));
		event_add(&i->_event, NULL);
		event_base_set(m_boruta->get_event_base(), &i->_event);
	}
}

void tcp_server_manager::connection_read_cb(struct bufferevent *bufev, void* _manager) {
	tcp_server_manager *m = (tcp_server_manager*) _manager;
	m->m_drivers.at(m->m_connections[bufev].serv_no)->data_ready(bufev);
}

void tcp_server_manager::connection_error_cb(struct bufferevent *bufev, short event, void* _manager) {
	tcp_server_manager *m = (tcp_server_manager*) _manager;
	connection &c = m->m_connections[bufev];
	m->m_drivers.at(c.serv_no)->connection_error(bufev);
	close(c.fd);
	m->m_connections.erase(bufev);
	bufferevent_free(bufev);
}

void tcp_server_manager::connection_accepted_cb(int _fd, short event, void* _listen_port) {
	struct sockaddr_in addr;
	socklen_t size = sizeof(addr);
	listen_port* p = (listen_port*) _listen_port;
	tcp_server_manager *m = p->manager;
do_accept:
	int fd = accept(_fd, (struct sockaddr*) &addr, &size);
	if (fd == -1) {
		if (errno == EINTR)
			goto do_accept;
		else if (errno == EAGAIN || errno == EWOULDBLOCK) 
			return;
		else if (errno == ECONNABORTED)
			return;
		else {
			dolog(0, "Accept error(%s), terminating application", strerror(errno));
			exit(1);
		}
	}
	dolog(5, "Connection from: %s", inet_ntoa(addr.sin_addr));
	connection c;
	c.serv_no = p->serv_no;
	c.fd = fd;
	c.bufev = bufferevent_new(fd,
			connection_read_cb, 
			NULL, 
			connection_error_cb,
			m);
	bufferevent_base_set(m->m_boruta->get_event_base(), c.bufev);
	bufferevent_enable(c.bufev, EV_READ | EV_WRITE | EV_PERSIST);
	m->m_connections[c.bufev] = c;
	m->m_drivers.at(p->serv_no)->connection_accepted(c.bufev);
}

int boruta_daemon::configure_ipc() {
	m_ipc = new IPCHandler(m_cfg);
	if (!m_cfg->GetSingle()) {
		if (m_ipc->Init())
			return 1;
		dolog(10, "IPC initialized successfully");
	} else {
		dolog(10, "Single mode, ipc not intialized!!!");
	}
	return 0;
}

int boruta_daemon::configure_events() {
	m_event_base = (struct event_base*) event_init();
	evtimer_set(&m_timer, cycle_timer_callback, this);
	event_base_set(m_event_base, &m_timer);
	return 0;
}

int boruta_daemon::configure_units() {
	int i, ret;
	TUnit* u;
	xmlXPathContextPtr xp_ctx = xmlXPathNewContext(m_cfg->GetXMLDoc());
	ret = xmlXPathRegisterNs(xp_ctx, BAD_CAST "ipk",
		SC::S2U(IPK_NAMESPACE_STRING).c_str());
	assert(ret == 0);
	ret = xmlXPathRegisterNs(xp_ctx, BAD_CAST "borutadmn",
		SC::S2U(IPK_NAMESPACE_STRING).c_str());
	assert(ret == 0);
	short *read = m_ipc->m_read;
	short *send = m_ipc->m_send;
	protocols _protocols;
	for (i = 0, u = m_cfg->GetDevice()->GetFirstRadio()->GetFirstUnit();
			u;
			++i, u = u->GetNext()) {
		std::stringstream ss;
		ss << "./ipk:unit[position()=" << i + 1 << "]";
		xmlNodePtr node = uxmlXPathGetNode((const xmlChar*) ss.str().c_str(), xp_ctx);
		std::string mode;
		if (get_xml_extra_prop(node, "mode", mode))
			return 1;
		bool server;
		if (mode == "server")
			server = true;	
		else if (mode == "client")
			server = false;	
                else {
			dolog(0, "Unknown unit mode: %s, failed to configure daemon", mode.c_str());
			return 1;
		}
		std::string medium;
		if (medium == "tcp") {
			if (server)
				ret = m_tcp_server_mgr.configure(u, node, read, send, _protocols);
			else
				ret = m_tcp_client_mgr.configure(u, node, read, send, _protocols);
			if (ret)
				return 1;
		} else if (medium == "serial") {
			if (server)
				ret = m_serial_server_mgr.configure(u, node, read, send, _protocols);
			else
				ret = m_serial_client_mgr.configure(u, node, read, send, _protocols);
			if (ret)
				return 1;
		} else {
			dolog(0, "Unknown connection type: %s, failed to configure daemon", medium.c_str());
			return 1;
		}
		read += u->GetParamsCount();
		send += u->GetSendParamsCount();
	}
	xmlXPathFreeContext(xp_ctx);
	return 0;
}


boruta_daemon::boruta_daemon() : m_tcp_client_mgr(this), m_tcp_server_mgr(this), m_serial_client_mgr(this), m_serial_server_mgr(this) {}

struct event_base* boruta_daemon::get_event_base() {
	return m_event_base;
}

int boruta_daemon::configure(DaemonConfig *cfg) {
	m_cfg = cfg;
	if (configure_ipc())
		return 1;
	if (configure_events())
		return 1;
	if (configure_units())
		return 1;
	return 0;
}

void boruta_daemon::go() {
	if (m_tcp_server_mgr.initialize())
		return;
	if (m_serial_server_mgr.initialize())
		return;
	if (m_tcp_client_mgr.initialize())
		return;
	if (m_serial_client_mgr.initialize())
		return;
	event_base_dispatch(m_event_base);
	cycle_timer_callback(-1, 0, this);
}

void boruta_daemon::cycle_timer_callback(int fd, short event, void* daemon) {
	boruta_daemon* b = (boruta_daemon*) daemon;
	b->m_ipc->GoSender();
	b->m_tcp_client_mgr.starting_new_cycle();
	b->m_tcp_server_mgr.starting_new_cycle();
	b->m_serial_client_mgr.starting_new_cycle();
	b->m_serial_server_mgr.starting_new_cycle();
	b->m_ipc->GoParcook();
	struct timeval tv;
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	evtimer_add(&b->m_timer, &tv); 
}

int main(int argc, char *argv[]) {
	DaemonConfig   *cfg;
	xmlInitParser();
	LIBXML_TEST_VERSION
	xmlLineNumbersDefault(1);
	cfg = new DaemonConfig("borutadmn");
	assert(cfg != NULL);
	if (cfg->Load(&argc, argv))
		return 1;
	g_debug = cfg->GetDiagno() || cfg->GetSingle();
	boruta_daemon daemon;
	if (daemon.configure(cfg))
		return 1;
	dolog(2, "Starting Boruta Daemon");
	daemon.go();
	dolog(0, "Error: daemon's event loop exited - that shouldn't happen!");
	return 1;
}

void dolog(int level, const char * fmt, ...) {
	va_list fmt_args;

	if (g_debug) {
		char *l;
		va_start(fmt_args, fmt);
		vasprintf(&l, fmt, fmt_args);
		va_end(fmt_args);

		std::cout << l << std::endl;
		sz_log(level, "%s", l);
		free(l);
	} else {
		va_start(fmt_args, fmt);
		vsz_log(level, fmt, fmt_args);
		va_end(fmt_args);
	}

} 