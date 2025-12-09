#include "uRemote.h"
#include "network.h"

NetworkManager network_manager;

int main() {
    json config;
    std::string local_ip;
    std::string title;
    std::string port;
    std::ifstream file(CONFIG);
    if (file.is_open()) {
        config = json::parse(file);
        local_ip = config["local_ip"];
        title = config["title"];
        port = config["port"];
        file.close();
    } else {
        local_ip = getDefaultGatewayIP();
        title = "uRemote\t" + local_ip;
        port = "9090";
        config["local_ip"] = local_ip;
        config["title"] = title;
        config["port"] = port;
        std::ofstream file(CONFIG);
        file << config.dump(4);
        file.close();
    }

	if (!glfwInit()) return -1;
	GLFWwindow* window = glfwCreateWindow(1280, 720, "uRemote", NULL, NULL);
	if (!window) {
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsLight();
	ImFontConfig font_cfg;
	font_cfg.SizePixels = 22.0f;
	io.Fonts->AddFontDefault(&font_cfg);


	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");


	ConnInputForm conn_input("", "");
    char port_input[6] = "";

    const bool show_main = true;
	bool main_active = true;
	int display_w, display_h;
	char error_text[128] = "";
    bool auto_scroll = true;
    bool show_status = true;
    bool show_connection_panel = true;
    bool show_messages_panel = true;

    bool show_settings = false;

	while (!glfwWindowShouldClose(window)) {
        ConnectionState state = network_manager.getConnectionState();
        std::string state_text = network_manager.getConnectionInfo();

		glfwPollEvents();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame(); 
		ImGui::NewFrame();

        if (show_main) {
            ImGui::Begin(title.c_str(), &main_active, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open..", "Ctrl+O")) {
                        // TODO: the last 10 recent connection
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Settings")) {
                    if (ImGui::MenuItem("Port")) {
                        show_settings = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            if (show_status) {
                // Connection Status Display
                ImGui::SeparatorText("Connection Status");
                // Color code based on connection state
                ImVec4 color;
                switch (state) {
                case ConnectionState::CONNECTED:
                    color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
                    break;
                case ConnectionState::CONNECTING:
                    color = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // Blue
                    break;
                case ConnectionState::ERR:
                    color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
                    break;
                default:
                    color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
                }
                ImGui::TextColored(color, "Status: %s", state_text.c_str());

                if (network_manager.isServerMode()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[SERVER MODE]");
                }
                if (network_manager.isClientMode()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.2f, 1.0f, 1.0f), "[CLIENT MODE]");
                }
            }

            if (ImGui::Button("Start Server")) {
                show_connection_panel = false;
                network_manager.startServer("9090");
            }

            if (show_connection_panel) {
                ImGui::InputTextWithHint("Connection Name", "<connection name>", conn_input.conn_name, IM_ARRAYSIZE(conn_input.conn_name));
                ImGui::InputTextWithHint("Host Machine", "<localhost>", conn_input.host_machine, IM_ARRAYSIZE(conn_input.host_machine));

                if (ImGui::BeginPopup("ConnctionInputError")) {
                    ImGui::Text("%s", error_text);
                    if (ImGui::Button("Close")) {
                        conn_input = { "","" };
                        error_text[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (ImGui::Button("Connect")) {
                    check_conn_input(&conn_input, error_text);
                    if (strlen(error_text)) {
                        ImGui::OpenPopup("ConnctionInputError");
                    }
                    else {
                        network_manager.startClient(conn_input.host_machine, port);
                    }
                }
                ImGui::SameLine(0, 10.0f);
                if (ImGui::Button("Cancel")) {
                    conn_input = { "","" };
                }
            }

            if (show_messages_panel && state == ConnectionState::CONNECTED) {
                ImGui::SeparatorText("Network Messages");

                // Message display area
                const float footer_height = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
                ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

                std::vector<std::string> messages = network_manager.getMessages();
                for (const auto& msg : messages) {
                    // Color code messages
                    if (msg.find("error") != std::string::npos) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    }
                    else if (msg.find("received") != std::string::npos) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                    }
                    else if (msg.find("Sent") != std::string::npos) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    }

                    ImGui::TextUnformatted(msg.c_str());
                    ImGui::PopStyleColor();
                }

                // Auto-scroll to bottom
                if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }

                ImGui::EndChild();

                // Message input
                static char message_input[256] = "";
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.8f);
                if (ImGui::InputTextWithHint("##MessageInput", "Type message here...",
                    message_input, IM_ARRAYSIZE(message_input),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (strlen(message_input) > 0) {
                        network_manager.sendMessage(message_input);
                        message_input[0] = '\0';
                    }
                    ImGui::SetKeyboardFocusHere(-1); // Auto-focus next frame
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                if (ImGui::Button("Send")) {
                    if (strlen(message_input) > 0) {
                        network_manager.sendMessage(message_input);
                        message_input[0] = '\0';
                    }
                }
            }

            ImGui::End();
        }
        
        if (show_settings) {
            ImGui::Begin("Settings", &show_settings, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text(("Current Using Port: " + port).c_str());
            ImGui::InputTextWithHint("Port", port.c_str(), port_input, IM_ARRAYSIZE(port_input), ImGuiInputTextFlags_CharsDecimal);

            if (ImGui::BeginPopup("PortError")) {
                ImGui::Text("%s", error_text);
                if (ImGui::Button("Close")) {
                    port_input[0] = '\0';
                    error_text[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (ImGui::Button("Apply")) {
                check_port(port_input, error_text);
                if (strlen(error_text)) {
                    ImGui::OpenPopup("PortError");
                } else {
                    port = port_input;
                    config["port"] = port;
                    std::ofstream file(CONFIG);
                    file << config.dump(4);
                    file.close();
                }
            }
            ImGui::SameLine(0, 10.0f);
            if (ImGui::Button("Close")) {
                port_input[0] = '\0';
                error_text[0] = '\0';
                show_settings = false;
            }
            ImGui::End();
        }

		ImGui::Render();
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}