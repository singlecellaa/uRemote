#pragma once
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <nlohmann/json.hpp>

#define CONFIG "config.json"

using namespace std;
using json = nlohmann::json;

struct ConnInputForm
{
	char conn_name[16];
	char host_machine[16];
	char port[6];
};

typedef struct ConnInputForm ConnRecord;

class ConnQueue {
private:
	std::list<ConnRecord> queue;  // Main queue storing records
	std::unordered_map<std::string, std::list<ConnRecord>::iterator> ipMap;  // Map for O(1) lookup
	const size_t MAX_SIZE = 10;

public:
	void push(const ConnRecord& newRecord) {
		std::string ip(newRecord.host_machine);

		auto it = ipMap.find(ip);
		if (it != ipMap.end()) {                   // If IP already exists, remove the old record
			queue.erase(it->second);               // Remove from list
			ipMap.erase(it);                       // Remove from map
		}  else if (queue.size() >= MAX_SIZE) {    // If queue is full and not a duplicate, remove the oldest (last)
			ipMap.erase(queue.back().host_machine);
			queue.pop_back();
		}
		queue.push_front(newRecord);
		ipMap[ip] = queue.begin();
	}

	const std::list<ConnRecord>& getRecords() const { return queue; }
	size_t size() const { return queue.size(); }
	bool empty() const { return queue.empty(); }

	void clear() {
		queue.clear();
		ipMap.clear();
	}
	bool contains(const std::string& ip) const {
		return ipMap.find(ip) != ipMap.end();
	}
	// Load queue from JSON
	void fromJson(const json& j) {
		clear();  // Clear existing data
		if (!j.is_array())  return;
		for (auto it = j.rbegin(); it != j.rend(); ++it) { // Load in reverse order to maintain correct order when pushed
			ConnRecord record;
			snprintf(record.conn_name, sizeof(record.conn_name), it->value("conn_name", "").c_str());
			snprintf(record.host_machine, sizeof(record.host_machine), it->value("host_machine", "").c_str());
			snprintf(record.port, sizeof(record.port), it->value("port", "").c_str());
			push(record);
		}
	}
	json toJson() const {
		json j = json::array();
		for (const auto& record : queue) {
			json recordJson;
			recordJson["conn_name"] = record.conn_name;
			recordJson["host_machine"] = record.host_machine;
			recordJson["port"] = record.port;
			j.push_back(recordJson);
		}
		return j;
	}
};

enum class SignalType {
	CONNECTED,
	DISCONNECTED,
	CMD_BUSY,
	CMD_IDLE,
	NONE
};

static inline void check_ip(const char* ip_addr, char* conn_err) {
	// Validate IPv4 format
	int octets[4];
	int num_octets = 0;
	char temp[16];
	strcpy(temp, ip_addr);

	char* token = strtok(temp, ".");
	while (token != NULL && num_octets < 4) {
		// Check if token contains only digits
		if (strspn(token, "0123456789") != strlen(token)) {
			strcpy(conn_err, "Invalid IP Address Format - Only digits and dots allowed");
			break;
		}
		octets[num_octets] = atoi(token);
		// Check if octet is within valid range
		if (octets[num_octets] < 0 || octets[num_octets] > 255) {
			strcpy(conn_err, "Invalid IP Address - Octets must be between 0 and 255");
			break;
		}
		// Check for leading zeros (except for single zero)
		if (strlen(token) > 1 && token[0] == '0') {
			strcpy(conn_err, "Invalid IP Address - No leading zeros allowed");
			break;
		}
		num_octets++;
		token = strtok(NULL, ".");
	}
	// Check if we have exactly 4 octets and no extra characters
	if (strlen(conn_err) == 0) {
		if (num_octets != 4) {
			strcpy(conn_err, "Invalid IP Address - Must have exactly 4 octets");
		}
		else {
			// Verify the original string format by reconstructing
			char reconstructed[16];
			snprintf(reconstructed, sizeof(reconstructed), "%d.%d.%d.%d",
				octets[0], octets[1], octets[2], octets[3]);
			if (strcmp(ip_addr, reconstructed) != 0) {
				strcpy(conn_err, "Invalid IP Address Format");
			}
		}
	}
}

static inline void check_port(char* port, char* err) {
	if (strlen(port) == 0) {
		strcpy(port, "9090");
	}
	else if (strspn(port, "0123456789") != strlen(port)) {
		strcpy(err, "Invalid Port Format");
	}
	else {
		int port_num = atoi(port);
		if (port_num < 1 || port_num > 65535) {
			strcpy(err, "Port Must Be Between 1 and 65535");
		}
	}
}

static inline void check_conn_input(ConnInputForm* input_form, char* conn_err) {
	if (strlen(input_form->conn_name) == 0) {
		strcpy(conn_err, "Please Input Connection Name");
	} else {
		if (strlen(input_form->host_machine) == 0) {
			strcpy(input_form->host_machine, "localhost");
		} else if (strcmp(input_form->host_machine, "localhost") != 0) {
			check_ip(input_form->host_machine, conn_err);
		}
		if (!strlen(conn_err)) {
			check_port(input_form->port, conn_err);
		}
	}
}
