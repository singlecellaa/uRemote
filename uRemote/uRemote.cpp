#include "uRemote.h"
#include "network.h"

class NetworkManager {
private:
    std::shared_ptr<Server> server_;
    std::shared_ptr<Client> client_;
    std::unique_ptr<boost::asio::io_context> server_io_context_;
    std::unique_ptr<boost::asio::io_context> client_io_context_;
    
    // Thread-safe message queue
    std::vector<std::string> received_messages_;
    std::mutex messages_mutex_;
    
    // Connection status
    std::atomic<ConnectionState> connection_state_{ConnectionState::DISCONNECTED};
    std::string connection_info_;
    mutable std::mutex info_mutex_;
    
    // Test message counter
    int test_message_counter_{0};

public:
    NetworkManager() = default;
    
    ~NetworkManager() {
        stopAll();
    }
    
    void startServer(const std::string& port) {
        stopAll();
        
        server_io_context_ = std::make_unique<boost::asio::io_context>();
        server_ = std::make_shared<Server>(*server_io_context_, port);
        
        server_->setConnectionCallback([this](ConnectionState state, const std::string& info) {
            handleConnectionState("Server", state, info);
        });
        
        server_->setMessageCallback([this](const NetworkMessage& message) {
            handleMessage("Server", message);
        });
        
        server_->setErrorCallback([this](const std::string& error) {
            handleError("Server", error);
        });
        
        server_->start();
        updateConnectionInfo("Server started on port " + port);
    }
    
    void startClient(const std::string& host, const std::string& port) {
        stopAll();
        
        client_io_context_ = std::make_unique<boost::asio::io_context>();
        client_ = std::make_shared<Client>(*client_io_context_, host, port);
        
        client_->setConnectionCallback([this](ConnectionState state, const std::string& info) {
            handleConnectionState("Client", state, info);
        });
        
        client_->setMessageCallback([this](const NetworkMessage& message) {
            handleMessage("Client", message);
        });
        
        client_->setErrorCallback([this](const std::string& error) {
            handleError("Client", error);
        });
        
        client_->start();
        updateConnectionInfo("Connecting to " + host + ":" + port + "...");
    }
    
    void stopAll() {
        if (server_) {
            server_->stop();
            server_.reset();
        }
        if (client_) {
            client_->stop();
            client_.reset();
        }
        server_io_context_.reset();
        client_io_context_.reset();
        
        setConnectionState(ConnectionState::DISCONNECTED);
        updateConnectionInfo("Disconnected");
    }
    
    void sendMessage(const std::string& message) {
        if (server_ && server_->isConnected()) {
            server_->send(message);
            addLocalMessage("Sent to client: " + message);
        } else if (client_ && client_->isConnected()) {
            client_->send(message);
            addLocalMessage("Sent to server: " + message);
        } else {
            addLocalMessage("Not connected - message not sent: " + message);
        }
    }
    
    void sendTestMessage() {
        std::stringstream ss;
        ss << "Test message #" << ++test_message_counter_ << " at " 
           << std::fixed << std::setprecision(2) << glfwGetTime();
        sendMessage(ss.str());
    }
    
    std::vector<std::string> getMessages() {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        return received_messages_;
    }
    
    void clearMessages() {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        received_messages_.clear();
    }
    
    ConnectionState getConnectionState() const {
        return connection_state_;
    }
    
    std::string getConnectionInfo() const {
        std::lock_guard<std::mutex> lock(info_mutex_);
        return connection_info_;
    }
    
    bool isConnected() const {
        return connection_state_ == ConnectionState::CONNECTED;
    }
    
    bool isServerMode() const {
        return server_ && server_->isConnected();
    }
    
    bool isClientMode() const {
        return client_ && client_->isConnected();
    }

private:
    void handleConnectionState(const std::string& type, ConnectionState state, const std::string& info) {
        setConnectionState(state);
        updateConnectionInfo(type + ": " + info);
        
        std::string message = type + " state: " + info;
        addLocalMessage(message);
        
        printf("%s\n", message.c_str());
    }
    
    void handleMessage(const std::string& type, const NetworkMessage& message) {
        std::string msg_str = message.toString();
        std::string display_msg = type + " received: " + msg_str;
        addLocalMessage(display_msg);
        printf("%s\n", display_msg.c_str());
    }
    
    void handleError(const std::string& type, const std::string& error) {
        std::string error_msg = type + " error: " + error;
        addLocalMessage(error_msg);
        printf("%s\n", error_msg.c_str());
    }
    
    void setConnectionState(ConnectionState state) {
        connection_state_ = state;
    }
    
    void updateConnectionInfo(const std::string& info) {
        std::lock_guard<std::mutex> lock(info_mutex_);
        connection_info_ = info;
    }
    
    void addLocalMessage(const std::string& message) {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        received_messages_.push_back(message);
        
        // Keep only last 100 messages to prevent memory growth
        if (received_messages_.size() > 100) {
            received_messages_.erase(received_messages_.begin());
        }
    }
};

NetworkManager network_manager;

int main() {
	io_context client_io_context;
	tcp::socket socket(client_io_context);
	tcp::resolver resolver(client_io_context);

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


	ConnInputForm conn_input("", "", "");
	bool main_active = 1;
	int display_w, display_h;
	char conn_error[128] = "";

    bool auto_scroll = true;
    bool show_status = true;
    bool show_connection_panel = true;
    bool show_messages_panel = true;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame(); 
		ImGui::NewFrame();

		ImGui::Begin("uRemote", &main_active, ImGuiWindowFlags_MenuBar);
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Open..", "Ctrl+O")) {
                    // TODO: the last 10 recent connection
				}
				ImGui::EndMenu();
			}
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::MenuItem("Port")) {
                    // TODO: change the port the app expose and use
                }
                ImGui::EndMenu();
            }
			ImGui::EndMenuBar();
		}
		
        ConnectionState state = network_manager.getConnectionState();
        std::string state_text = network_manager.getConnectionInfo();

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
            ImGui::InputTextWithHint("Port", "<9090>", conn_input.port, IM_ARRAYSIZE(conn_input.port), ImGuiInputTextFlags_CharsDecimal);

            if (ImGui::BeginPopup("ConnctionInputError")) {
                ImGui::Text("%s", conn_error);
                if (ImGui::Button("Close")) {
                    conn_input = { "","","" };
                    conn_error[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::Button("Connect")) {
                check_conn_input(&conn_input, conn_error);
                if (strlen(conn_error)) {
                    ImGui::OpenPopup("ConnctionInputError");
                }
                else {
                    network_manager.startClient(conn_input.host_machine, conn_input.port);
                }
            }
            ImGui::SameLine(0, 10.0f);
            if (ImGui::Button("Cancel")) {
                conn_input = { "","","" };
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