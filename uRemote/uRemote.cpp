#include "uRemote.h"
#include "network.h"
#include "cli.h"

NetworkManager network_manager;
ConnQueue recent_conn;
ProcessManager cmd;

int main() {
    json config;
    std::string local_ip = getLocalConnectedIP();
    std::string port;
    std::string download_path;
    std::string password;

    std::ifstream file(CONFIG);
    if (file.is_open()) {
        config = json::parse(file);
        port = config.value("port", "");
        download_path = config.value("download_path", "");
        password = config.value("password", "");
        json recent_conn_json = config.value("recent_conn", json::array());
        recent_conn.fromJson(recent_conn_json);
        file.close();
        if (port.empty() || download_path.empty() || password.empty()) {
            if (port.empty()) config["port"] = port =  "9090";
            if (download_path.empty()) config["download_path"] = download_path = "C:\\uRemote";
            if (password.empty()) config["password"] = password = "123456";
            std::ofstream file(CONFIG);
            file << config.dump(4);
            file.close();
        }
    }
    else {
        config["port"] = port = "9090";
        config["download_path"] = download_path = "C:\\uRemote";
        config["password"] = password = "123456";
        config["recent_conn"] = json::array();
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


    const bool show_main = true;
    bool main_active = true;
    int display_w, display_h;

    bool show_status = true;
    Mode mode = Mode::NONE;
    ConnectionState state = ConnectionState::DISCONNECTED;
    std::string state_text;

    bool show_server_panel = true;
    bool show_connection_panel = true;
    ConnInputForm conn_input("", "", "", "");
    char port_input[6] = "";
    char error_text[128] = "";
	bool authentication_failed = false;

    bool running = false;
    std::vector<std::string> server_output_vec;

    bool show_messages_panel = true;
    bool auto_scroll = true;

    bool show_settings = false;
    SettingType current_setting = SettingType::PORT;

    char download_path_input[512] = "";
    char password_input[128] = "";

    bool show_recent_conn = false;

    bool show_cli = true;
    std::vector<std::string> cli_logs;
    bool cmd_busy = false;
    char cli_input[256] = "";
    bool new_input = false;
    std::vector<std::string> client_output_vec;
    bool new_log = false;
    bool scroll = false;

    // File Explorer variables
    bool show_file_explorer = true;
    std::string current_path = "";
    DirectoryListing current_directory;
    char path_input[512] = "";
    bool show_filesystem_error = false;
    std::string filesystem_error_msg;

    // File Viewer variables
    bool show_file_viewer = false;
    std::string file_viewer_title = "";
    std::string file_viewer_content = "";

    // Remote Desktop variables
    bool show_remote_desktop = true;
    std::vector<uint8_t> screenshot_buffer;
    int screenshot_width = 0;
    int screenshot_height = 0;
    GLuint screenshot_texture = 0;
    bool screenshot_updated = false;

    while (!glfwWindowShouldClose(window)) {
        state = network_manager.getConnectionState();
        running = state == ConnectionState::CONNECTING || state == ConnectionState::AUTHENTICATING || state == ConnectionState::CONNECTED;
        state_text = network_manager.getConnectionInfo();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
        if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS && (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS))
            show_recent_conn = true;
        glfwPollEvents();
        auto network_signals = network_manager.popSignals();
        for (const auto& signal : network_signals) {
            switch (signal) {
            case SignalType::AUTHENTICATION_FAILED:
                Sleep(1000);
				network_manager.stopAll();
				authentication_failed = true;
                break;
            case SignalType::CONNECTED: {
                recent_conn.push(conn_input);
                config["recent_conn"] = recent_conn.toJson();
                std::ofstream file(CONFIG);
                file << config.dump(4);
                file.close();
                // Request initial directory listing for file explorer
                if (mode == Mode::CLIENT) {
                    NetworkMessage request;
                    request.fromFilesystemRequest();
                    network_manager.sendMessage(request);
                }
                break;
            }
            case SignalType::DISCONNECTED:
                if (cmd.isRunning()) {
                    cmd.stop();
                }
                break;
            default:
                break;
            }
        }
        auto cmd_signals = cmd.popSignals();
        for (const auto& signal : cmd_signals) {
            switch (signal) {
            case SignalType::CMD_BUSY:
            case SignalType::CMD_IDLE: {
                NetworkMessage signal_message;
                signal_message.fromSignal(signal);
                network_manager.sendMessage(signal_message);
				std::cout << "Sent signal to network: " << (signal == SignalType::CMD_BUSY ? "CMD_BUSY" : "CMD_IDLE") << std::endl;
                break;
            }
            default:
                break;
            }
        }

        auto network_messages = network_manager.popNetworkMessages();
        for (const auto& msg : network_messages) {
            switch (msg.type) {
            case MessageType::COMMAND:
                if (mode == Mode::SERVER && cmd.isRunning()) {
                    std::cout << "Server sent command to cmd: " << msg.toString() << std::endl;
                    cmd.sendCommand(msg.toString());
                }
                break;
            case MessageType::TERMIAL_OUTPUT:
                if (mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
                    client_output_vec.push_back(msg.toString());
					std::cout << "terminal received output: " << msg.toString() << std::endl;
                }
                break;
            case MessageType::SIGNAL: {
                SignalType signal = msg.toSignal();
				std::cout << "received signal from network: " << (signal == SignalType::CMD_BUSY ? "CMD_BUSY" : "CMD_IDLE") << std::endl;
                if (signal == SignalType::CMD_BUSY)
                    cmd_busy = true;
                else if (signal == SignalType::CMD_IDLE)
                    cmd_busy = false;
				std::cout << "terminal cmd_busy set to: " << (cmd_busy ? "true" : "false") << std::endl;
                break;
            }
            case MessageType::FILESYSTEM_REQUEST:
                if (mode == Mode::SERVER) {
                    std::string requestedPath = msg.toFilesystemRequest();
                    if (requestedPath.empty()) 
                        requestedPath = std::getenv("USERPROFILE");
                    std::cout << "Server received filesystem request for path: " << requestedPath << std::endl;
                    auto [success, listing] = getDirectoryListing(requestedPath);
                    NetworkMessage response;
                    if (success) {
                        response.fromDirectoryListing(listing);
                    } else {
                        response.fromError("Path not found: " + requestedPath);
                    }
                    network_manager.sendMessage(response);
                }
                break;
            case MessageType::FILE_CONTENT_REQUEST:
                if (mode == Mode::SERVER) {
                    std::string requestedPath = msg.toFileContentRequest();
                    std::cout << "Server received file content request for path: " << requestedPath << std::endl;
                    auto [success, content] = readFileContent(requestedPath);
                    NetworkMessage response;
                    if (success) {
                        FileResponse fr;
                        fr.filename = std::filesystem::path(requestedPath).filename().string();
                        fr.content = content;
                        response.fromFileContentResponse(fr);
                    } else {
                        response.fromError("Failed to read file: " + requestedPath);
                    }
                    network_manager.sendMessage(response);
                }
                break;
            case MessageType::FILE_DOWNLOAD_REQUEST:
                if (mode == Mode::SERVER) {
                    std::string requestedPath = msg.toFileDownloadRequest();
                    std::cout << "Server received file download request for path: " << requestedPath << std::endl;
                    auto [success, content] = readFileContent(requestedPath);
                    NetworkMessage response;
                    if (success) {
                        FileResponse fr;
                        fr.filename = std::filesystem::path(requestedPath).filename().string();
                        fr.content = content;
                        response.fromFileDownloadResponse(fr);
                    } else {
                        response.fromError("Failed to read file: " + requestedPath);
                    }
                    network_manager.sendMessage(response);
                }
                break;
            case MessageType::FILESYSTEM_RESPONSE:
                if (mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
                    current_directory = msg.toDirectoryListing();
                    current_path = current_directory.path;
                    path_input[0] = '\0'; // Clear the input field
                    std::cout << "Client received filesystem response for path: " << current_path << " with " << current_directory.files.size() << " items" << std::endl;
                }
                break;
            case MessageType::FILE_CONTENT_RESPONSE:
                if (mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
                    FileResponse response = msg.toFileContentResponse();
                    file_viewer_content = std::string(response.content.begin(), response.content.end());
                    file_viewer_title = "File Viewer - " + response.filename;
                    show_file_viewer = true;
                    std::cout << "Client received file content response for " << response.filename << " with " << response.content.size() << " bytes" << std::endl;
                }
                break;
            case MessageType::FILE_DOWNLOAD_RESPONSE:
                if (mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
                    FileResponse response = msg.toFileDownloadResponse();
                    // Save to download path
                    std::filesystem::path downloadDir(download_path);
                    if (!std::filesystem::exists(downloadDir)) {
                        std::filesystem::create_directories(downloadDir);
                    }
                    std::filesystem::path filePath = downloadDir / response.filename;
                    std::ofstream outFile(filePath, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<const char*>(response.content.data()), response.content.size());
                        outFile.close();
                        filesystem_error_msg = "Download completed: " + response.filename;
                    } else {
                        filesystem_error_msg = "Failed to save file: " + response.filename;
                    }
                    show_filesystem_error = true;
                    std::cout << "Client received file download response for " << response.filename << " with " << response.content.size() << " bytes" << std::endl;
                }
                break;
            case MessageType::ERR:
                if (mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
                    filesystem_error_msg = msg.toError();
                    show_filesystem_error = true;
                    std::cout << "Client received error: " << filesystem_error_msg << std::endl;
                }
                break;
            case MessageType::SCREENSHOT_REQUEST:
                if (mode == Mode::SERVER) {
                    std::cout << "Server received screenshot request" << std::endl;
                    auto [success, response] = captureScreenshot();
                    NetworkMessage msg;
                    if (success) {
                        msg.fromScreenshotResponse(response);
                    } else {
                        msg.fromError("Failed to capture screenshot");
                    }
                    network_manager.sendMessage(msg);
                }
                break;
            case MessageType::SCREENSHOT_RESPONSE:
                if (mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
                    ScreenshotResponse response = msg.toScreenshotResponse();
                    screenshot_buffer = response.data;
                    screenshot_width = response.width;
                    screenshot_height = response.height;
                    screenshot_updated = true;
                    std::cout << "Client received screenshot response with " << screenshot_buffer.size() << " bytes, " << screenshot_width << "x" << screenshot_height << std::endl;
                }
                break;
            default:
				std::cout << "unknown message type received from network" << std::endl;
                break;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (show_main) {
            ImGui::Begin(("uRemote\t" + local_ip).c_str(), &main_active, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open..", "Ctrl+O")) {
                        show_recent_conn = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Settings")) {
                    if (ImGui::MenuItem("Port")) {
                        current_setting = SettingType::PORT;
                        show_settings = true;
                    }
                    if (ImGui::MenuItem("Download Path")) {
                        current_setting = SettingType::DOWNLOAD_PATH;
                        show_settings = true;
                    }
                    if (ImGui::MenuItem("Password")) {
                        current_setting = SettingType::PASSWORD;
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
                case ConnectionState::AUTHENTICATING:
                    color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
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

            if (show_server_panel && !running) {
                if (ImGui::Button("Start Server")) {
                    mode = Mode::SERVER;
                    conn_input = { "","","","" };
                    network_manager.startServer(port, password);
                    if (!cmd.isRunning()) {
                        cmd.start();
                    }
                }
            }

            if (running) {
                if (ImGui::Button("Stop")) {
                    network_manager.stopAll();
                    mode = Mode::NONE;
                }
                if (mode == Mode::SERVER && cmd.isRunning()) {
                    //send output by network manager server to client
                    std::vector<std::string> cmd_output = cmd.getOutput();
                    if (cmd_output.size() > 0) {
						server_output_vec.insert(server_output_vec.end(), cmd_output.begin(), cmd_output.end());
						std::cout << "server get " << cmd_output.size() << " outputs from cmd" << std::endl;
					}
                    if (state == ConnectionState::CONNECTED) {
                        for (const auto& output : server_output_vec) {
                            NetworkMessage msg;
                            msg.type = MessageType::TERMIAL_OUTPUT;
                            msg.data.assign(output.begin(), output.end());
                            std::cout << "Server send output to client: " << output << std::endl;
                            network_manager.sendMessage(msg);
                        }
						server_output_vec.clear();
                    }
                }
            }

            if (show_connection_panel && !running) {
                ImGui::InputTextWithHint("Connection Name", "<connection name>", conn_input.conn_name, IM_ARRAYSIZE(conn_input.conn_name));
                ImGui::InputTextWithHint("Host Machine", "<localhost>", conn_input.host_machine, IM_ARRAYSIZE(conn_input.host_machine));
                ImGui::InputTextWithHint("Port", port.c_str(), conn_input.port, IM_ARRAYSIZE(conn_input.port), ImGuiInputTextFlags_CharsDecimal);
                ImGui::InputTextWithHint("Password", "<password>", conn_input.password, IM_ARRAYSIZE(conn_input.password));

                if (ImGui::BeginPopup("ConnctionInputError")) {
                    ImGui::Text("%s", error_text);
                    if (ImGui::Button("Close")) {
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
                        mode = Mode::CLIENT;
                        network_manager.startClient(conn_input.host_machine, conn_input.port, conn_input.password);
                    }
                }
                if (authentication_failed) {
                    strcpy(error_text, "Authentication failed. Please check your password.");
					ImGui::OpenPopup("ConnctionInputError");
					authentication_failed = false;
                }


                ImGui::SameLine(0, 10.0f);
                if (ImGui::Button("Cancel")) {
                    conn_input = { "","","","" };
                }
            }

            if (show_messages_panel && state == ConnectionState::CONNECTED) {
                ImGui::SeparatorText("Network Messages");

                // Message display area
                const float footer_height = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
                ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height), false, ImGuiWindowFlags_HorizontalScrollbar);

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
                if (ImGui::InputTextWithHint("##MessageInput", "Type message here...", message_input, IM_ARRAYSIZE(message_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
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
            switch (current_setting) {
            case SettingType::PORT: {
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
                    }
                    else {
                        port = port_input;
                        config["port"] = port;
                        std::ofstream file(CONFIG);
                        file << config.dump(4);
                        file.close();
                    }
                }
                break;
            }
            case SettingType::DOWNLOAD_PATH: {
                ImGui::Text(("Current Download Path: " + (download_path.empty() ? "Not set" : download_path)).c_str());
                ImGui::InputTextWithHint("Download Path", "Enter path...", download_path_input, IM_ARRAYSIZE(download_path_input));

                if (ImGui::Button("Apply")) {
                    download_path = download_path_input;
                    config["download_path"] = download_path;
                    std::ofstream file(CONFIG);
                    file << config.dump(4);
                    file.close();
                }
                break;
            }
            case SettingType::PASSWORD: {
                ImGui::Text(("Current Password: " + (password.empty() ? "Not set" : password)).c_str());
                ImGui::InputTextWithHint("Password", "Enter password...", password_input, IM_ARRAYSIZE(password_input));

                if (ImGui::Button("Apply")) {
                    password = password_input;
                    config["password"] = password;
                    std::ofstream file(CONFIG);
                    file << config.dump(4);
                    file.close();
                }
                break;
            }
            }
            ImGui::SameLine(0, 10.0f);
            if (ImGui::Button("Close")) {
                port_input[0] = '\0';
                download_path_input[0] = '\0';
                password_input[0] = '\0';
                error_text[0] = '\0';
                show_settings = false;
            }
            ImGui::End();
        }

        if (show_recent_conn) {
            ImGui::Begin("Recent Connections", &show_recent_conn, ImGuiWindowFlags_AlwaysAutoResize);
            std::list<ConnRecord>records = recent_conn.getRecords();
            int index = 0;

            for (auto& record : records) {
                ImGui::PushID(index);

                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

                ImGui::BeginChild("##item",
                    ImVec2(600.f, ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2 + 8.f),
                    true // Border
                );

                bool clicked = ImGui::Selectable("##selectable", false, 0, ImGui::GetContentRegionAvail());

                ImVec2 cursorPos = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(cursorPos);

                // Draw text content
                ImGui::Text("%10s", record.conn_name);
                ImGui::SameLine();
                ImGui::Text("%23s", record.host_machine);
                ImGui::SameLine();
                ImGui::Text("%12s", record.port);
                ImGui::EndChild();
                ImGui::PopStyleVar(1);

                if (clicked) {
                    conn_input = record;
                    network_manager.startClient(record.host_machine, record.port, record.password);
					mode = Mode::CLIENT;
                    show_recent_conn = false;
                }

                ImGui::PopID();
                index++;
            }
            if (ImGui::Button("Close")) {
                show_settings = false;
            }
            ImGui::End();
        }

        if (show_cli && state == ConnectionState::CONNECTED && mode == Mode::CLIENT) {
            ImGui::StyleColorsDark();
            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
            ImGui::Begin("Command Line Interface", &show_cli);

            for (const auto& log : cli_logs) {
                if (log.length()) {
                    // Split log by newlines to handle multiline output properly
                    std::stringstream ss(log);
                    std::string line;
                    while (std::getline(ss, line)) {
                        if (!line.empty()) {
                            ImGui::TextWrapped("%s", line.c_str());
                        }
                    }
                }
            }

            if (!cmd_busy) {
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 10);
                if (ImGui::InputText("##Input", cli_input, IM_ARRAYSIZE(cli_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    std::string command(cli_input);
                    cli_input[0] = '\0';
                    // send command to by network manager client
                    NetworkMessage msg;
                    msg.type = MessageType::COMMAND;
                    msg.data.assign(command.begin(), command.end());
                    network_manager.sendMessage(msg);
                    new_input = true;
                }
                ImGui::PopItemWidth();
            }
            // get output from network manager client sent by server
            if (client_output_vec.size()) {
                for (size_t i = 0; i < client_output_vec.size(); ++i) {
                    if (new_input) {
                        cli_logs.back() += client_output_vec[i];
                        new_input = false;
                    }
                    else
                        cli_logs.emplace_back(client_output_vec[i]);
                }
                client_output_vec.clear();
                if (cli_logs.size() > 200)
                    cli_logs.erase(cli_logs.begin());
                new_log = true;
            }
            if (scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
                scroll = false;
            }
            if (new_log) {
                new_log = false;
                scroll = true; //scroll next round, the logs are updated
            }
            ImGui::End();
            ImGui::StyleColorsLight();
        }

        // File Explorer Panel
        if (show_file_explorer && mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
            ImGui::Begin("File Explorer", &show_file_explorer);
            
            // Current path display and navigation
            if (ImGui::InputTextWithHint("##path", current_path.c_str(), path_input, IM_ARRAYSIZE(path_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
                NetworkMessage request;
                request.fromFilesystemRequest(path_input);
                network_manager.sendMessage(request);
                // Clear the input after sending
                path_input[0] = '\0';
            }
            ImGui::Separator();
            
            // Navigation buttons
            if (ImGui::Button("Up")) {
                std::filesystem::path p(current_path);
                if (p.has_parent_path()) {
                    std::string parentPath = p.parent_path().string();
                    NetworkMessage request;
                    request.fromFilesystemRequest(parentPath);
                    network_manager.sendMessage(request);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                NetworkMessage request;
                request.fromFilesystemRequest(current_path);
                network_manager.sendMessage(request);
            }
            ImGui::SameLine();
            if (ImGui::Button("Root")) {
                NetworkMessage request;
                request.fromFilesystemRequest("");
                network_manager.sendMessage(request);
            }
            
            ImGui::Separator();
            
            // File list
            if (ImGui::BeginChild("FileList", ImVec2(0, 0), true)) {
                for (const auto& file : current_directory.files) {
                    bool isSelected = false;
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    
                    if (file.isDirectory) {
                        flags |= ImGuiTreeNodeFlags_OpenOnDoubleClick;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.6f, 1.0f, 1.0f)); // Blue for directories
                    }
                    
                    bool nodeOpen = ImGui::TreeNodeEx(file.name.c_str(), flags);
                    
                    if (file.isDirectory) {
                        ImGui::PopStyleColor();
                    }
                    
                    // Handle double-click to navigate into directories
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && file.isDirectory) {
                        std::filesystem::path newPath = std::filesystem::path(current_path) / file.name;
                        NetworkMessage request;
                        request.fromFilesystemRequest(newPath.string());
                        network_manager.sendMessage(request);
                    }
                    
                    // Context menu
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Open", NULL, false, !file.isDirectory && isTextFile(file.name))) {
                            if (!file.isDirectory && isTextFile(file.name)) {
                                std::filesystem::path filePath = std::filesystem::path(current_path) / file.name;
                                NetworkMessage request;
                                request.fromFileContentRequest(filePath.string());
                                network_manager.sendMessage(request);
                            }
                        }
                        if (ImGui::MenuItem("Download", NULL, false, !file.isDirectory)) {
                            if (!file.isDirectory) {
                                std::filesystem::path filePath = std::filesystem::path(current_path) / file.name;
                                NetworkMessage request;
                                request.fromFileDownloadRequest(filePath.string());
                                network_manager.sendMessage(request);
                            }
                        }
                        ImGui::EndPopup();
                    }
                    
                    // Show file info in tooltip
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("Name: %s", file.name.c_str());
                        ImGui::Text("Type: %s", file.isDirectory ? "Directory" : "File");
                        if (!file.isDirectory) {
                            ImGui::Text("Size: %zu bytes", file.size);
                        }
                        ImGui::Text("Modified: %s", file.lastModified.c_str());
                        ImGui::EndTooltip();
                    }
                }
            }
            ImGui::EndChild();
            
            // Error popup
            if (show_filesystem_error) {
                ImGui::OpenPopup("FilesystemError");
                show_filesystem_error = false; // Reset flag
            }
            if (ImGui::BeginPopup("FilesystemError")) {
                ImGui::Text("%s", filesystem_error_msg.c_str());
                if (ImGui::Button("Close")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            
            ImGui::End();
        }

        // File Viewer Panel
        if (show_file_viewer) {
            ImGui::Begin(file_viewer_title.c_str(), &show_file_viewer);
            ImGui::TextWrapped("%s", file_viewer_content.c_str());
            ImGui::End();
        }

        if (show_remote_desktop && mode == Mode::CLIENT && state == ConnectionState::CONNECTED) {
            ImGui::Begin("Remote Desktop", &show_remote_desktop);
            if (ImGui::Button("Request Screenshot")) {
                NetworkMessage request;
                request.fromScreenshotRequest();
                network_manager.sendMessage(request);
            }
            if (!screenshot_buffer.empty()) {
                if (screenshot_updated) {
                    if (screenshot_texture != 0) {
                        glDeleteTextures(1, &screenshot_texture);
                    }
                    glGenTextures(1, &screenshot_texture);
                    glBindTexture(GL_TEXTURE_2D, screenshot_texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, screenshot_width, screenshot_height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, screenshot_buffer.data());
                    screenshot_updated = true;
                }
                ImGui::Image((ImTextureID)(intptr_t)screenshot_texture, ImVec2(screenshot_width * 0.5f, screenshot_height * 0.5f));
            } else {
                ImGui::Text("No screenshot received yet.");
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