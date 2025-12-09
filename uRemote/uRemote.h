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
};

struct ConnRecord
{
	char conn_name[16];
	char host_machine[16];
	char port[6];
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
	}
}