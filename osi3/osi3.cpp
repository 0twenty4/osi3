#include <ws2tcpip.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <ranges>
#include <fstream>
#include <iomanip>
#include <codecvt>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

enum {
	CLIENT_EXIT = 0,
	CLIENT_REGISTER,
	CLIENT_SCROLL_UP,
	CLIENT_SCROLL_DOWN,
	SUCCESS
};

struct CLIENT {
	SOCKET socket;

	std::string name;
	std::string ip;
	std::string port;

	int window_scroll_offset = 0;

	CLIENT(SOCKET socket, const char* ip, const std::string& port) : socket(socket), ip(ip), port(port) {};
};

struct SERVER {
	SOCKET socket;
	SOCKADDR_IN sockaddr;

	std::unordered_map<SOCKET,CLIENT> clients;
	std::mutex message_rec_mtx;

	std::fstream messages_log;

	SERVER() {
		messages_log.open("messages_log.txt", std::ios::in | std::ios::out);
	}

	int message_count = 0;
	int display_message_count = 20;

};

SERVER server;

std::string utf16_to_utf8(const std::wstring& utf16_string) {

	int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16_string.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string utf8_string(size_needed - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, utf16_string.c_str(), -1, &utf8_string[0], size_needed, nullptr, nullptr);

	return utf8_string;
}

std::wstring utf8_to_utf16(const std::string& utf8_string) {

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8_string.c_str(), -1, nullptr, 0);
	std::wstring utf16_string(size_needed - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8_string.c_str(), -1, &utf16_string[0], size_needed);

	return utf16_string;
}

void server_echo(const std::wstring& message, const CLIENT& client) {
	std::cout << "Message from " << client.ip << ":" << client.port << " - ";
	std::wcout << message << std::endl;
}

int send_message_to_sender(std::string message, CLIENT& client) {

	int ret_val = send(client.socket, message.data(), message.size(), 0);

	std::cout << "message to send " << message << std::endl;

	if (ret_val == SOCKET_ERROR) {
		std::cout << "Sending failed with error: " << WSAGetLastError();
		closesocket(client.socket);
		return 0;
	}
}

int send_message_to_all(std::string message, CLIENT* except_client = nullptr) {
	int ret_val;

	for (const auto& client : std::views::values(server.clients)) {

		if (&client != except_client) {

			if (client.window_scroll_offset == 0) {

				ret_val = send(client.socket, message.data(), message.size(), 0);

				if (ret_val == SOCKET_ERROR) {
					std::cout << "Sending failed with error: " << WSAGetLastError();
					closesocket(client.socket);
					return 0;
				}
			}
		}
	}
}

std::string get_message_from_log(int aim_message_index) {
	server.messages_log.seekg(0, std::ios::beg);

	std::string message;

	for (int message_index = 0; message_index <= aim_message_index; message_index++)
		std::getline(server.messages_log, message);

	return message;
}

int process_command_message(const std::string& command_message, CLIENT& client) {
	std::string command;
	std::istringstream command_stream(command_message);

	command_stream >> command;

	if (command == "/register") {
		command_stream >> client.name;

		std::cout << "rigister" << std::endl;

		std::string chat_message = "Client " + client.name + "(" + client.ip + ":" + client.port + ") has joined the chat";
		server.messages_log << chat_message << std::endl;;

		send_message_to_all(chat_message);

		server.message_count++;

		return CLIENT_REGISTER;
	}

	if (command == "/scroll_up") {
		int offset;
		command_stream >> offset;

		int aim_message_index = server.message_count - server.display_message_count - offset;

		std::cout << "aim_message_index " << aim_message_index << std::endl;

		if (aim_message_index >= 0) {

			send_message_to_sender("/scroll_up " + get_message_from_log(aim_message_index), client);
		}

		return CLIENT_SCROLL_UP;
	}

	if (command == "/scroll_down") {
		int offset;
		command_stream >> offset;

		int aim_message_index = server.message_count - offset;

		if (aim_message_index < server.message_count) {

			send_message_to_sender("/scroll_down " + get_message_from_log(aim_message_index), client);
		}

		return CLIENT_SCROLL_DOWN;
	}

	if (command == "/exit") {


		std::string chat_message = "Client " + client.name + " (" + client.ip + ":" + client.port + ") has left the chat";
		server.messages_log << chat_message << std::endl;
		send_message_to_all(chat_message, &client);
		server.message_count++;

		std::cout << "Client " << client.ip << ":" << client.port << " has disconnected" << std::endl;

		server.clients.erase(client.socket);
		closesocket(client.socket);

		return CLIENT_EXIT;
	}

	send_message_to_sender("Unknown command", client);

	return 0;
}

int listen_client(CLIENT& client) {
	setlocale(LC_ALL, "rus");

	int ret_val;
	std::string message_utf8;
	std::wstring message_utf16;

	while (true) {
		message_utf8.clear();
		message_utf8.resize(1024);

		std::cout << "Receiving data from client" << client.ip << std::endl;

		ret_val = recv(client.socket, (char*)message_utf8.data(), message_utf8.size(), 0);

		server.message_rec_mtx.lock();

		if (ret_val == SOCKET_ERROR) {
			process_command_message("/exit", client);

			server.message_rec_mtx.unlock();

			return 0;
		}

		message_utf8.resize(ret_val);

		message_utf16 = utf8_to_utf16(message_utf8);
			
		server_echo(message_utf16, client);

		if (message_utf8.data()[0] == '/') {
			int ret_val = process_command_message(message_utf8, client);

			if (ret_val == CLIENT_EXIT) {
				server.message_rec_mtx.unlock();
				return SUCCESS;
			}
		}
		else {
			message_utf8 = client.name + ": " + message_utf8;
			server.messages_log << message_utf8 << std::endl;
			send_message_to_all(message_utf8);

			server.message_count++;
		}

		server_echo(message_utf16, client);

		server.message_rec_mtx.unlock();
	}
}

int process_server_socket() {

	int clients_count = 15;
	int ret_val;

	while (true) {
		std::cout << "Listening to the server socket" << std::endl;

		ret_val = listen(server.socket, clients_count);

		if (ret_val == SOCKET_ERROR) {
			std::cout << "Listening to the server socket failed with error: " << WSAGetLastError();
			closesocket(server.socket);
			WSACleanup();
			return 0;
		}

		std::cout << "Accepting the client socket" << std::endl;

		SOCKET client_socket;
		SOCKADDR_IN client_sockaddr;

		int client_addr_size = sizeof(client_sockaddr);

		client_socket = accept(server.socket, (sockaddr*)&client_sockaddr, &client_addr_size);

		if (client_socket == INVALID_SOCKET) {
			std::cout << "Accepting failed with error: " << WSAGetLastError();
			closesocket(server.socket);
			WSACleanup();
			return 0;
		}

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_sockaddr.sin_addr, client_ip, INET_ADDRSTRLEN);

		CLIENT client(client_socket, client_ip, std::to_string(ntohs(client_sockaddr.sin_port)));

		auto [pair_socket_client, emplaced] = server.clients.emplace(client.socket, std::move(client));

		std::thread client_socket_listener(listen_client, std::ref(pair_socket_client->second));

		client_socket_listener.detach();

	}
}

int start_server() {
	PCSTR server_ip = "192.168.0.104";
	int port = 2011;
	int clients_count = 15;
	int recv_size = 2;
	int send_size = 1;
	int ret_val;

	WSAData wsa_data;
	if (WSAStartup(MAKEWORD(2, 2, ), &wsa_data) != NO_ERROR) {
		std::cout << "WSAStartup failed" << std::endl;
		return 0;
	}

	server.sockaddr.sin_family = AF_INET;
	server.sockaddr.sin_port = htons(port);
	inet_pton(AF_INET, server_ip, &server.sockaddr.sin_addr);

	std::cout << "Creating the server socket" << std::endl;

	server.socket= socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (server.socket == INVALID_SOCKET) {
		std::cout << "Socket creation failed with error: " << WSAGetLastError();
		WSACleanup();
		return 0;
	}

	std::cout << "Binding the server socket" << std::endl;

	int option = 1;
	setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(option));


	ret_val = bind(server.socket, (sockaddr*)&server.sockaddr, sizeof(server.sockaddr));

	if (ret_val == SOCKET_ERROR) {
		std::cout << "Binding server socket failed with error: " << WSAGetLastError();
		closesocket(server.socket);
		WSACleanup();
		return 0;
	}
	
	process_server_socket();
}




int main() {
	setlocale(LC_ALL, "rus");
	start_server();
}